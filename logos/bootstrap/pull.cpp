#include <logos/blockstore.hpp>
#include <logos/node/common.hpp>
#include <logos/node/node.hpp>
#include <logos/node/utility.hpp>

#include <logos/bootstrap/pull.hpp>
#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/attempt.hpp>

namespace Bootstrap
{
    std::string MBRequestTips_to_string (MBPtr block)
    {
        std::stringstream stream;
        stream << " MB tip:" << block->CreateTip().to_string() <<std::endl;
        stream << " MB request tips:" <<std::endl;
        for(int i = 0; i < NUM_DELEGATES; ++i) {
            stream << " i=" << i << "  " << block->tips[i].to_string() << std::endl;
        }

        return stream.str ();
    }

    Puller::Puller(logos::IBlockCache & block_cache, logos::alarm & alarm)
    : block_cache(block_cache)
    , alarm(alarm)
    , num_blocks_to_download(0)
    , inited(false)
    {
        LOG_TRACE(log) << "Puller::"<<__func__;
    }

    bool Puller::Init(AttemptPtr a, const TipSet & my, const TipSet & others)
    {
        LOG_INFO(log) << "Puller::"<<__func__;
        std::lock_guard<std::mutex> lck (mtx);
        inited = true;
        this->attempt = a;
        this->my_tips = my;
        this->others_tips = others;

        LOG_TRACE(log) << "Puller::"<<__func__ << " my_tips " << "\n" << my_tips;
        LOG_TRACE(log) << "Puller::"<<__func__ << " others_tips " << "\n" << others_tips;

        if(! my_tips.ValidPeerTips(others_tips))
        {
            LOG_WARN(log) << "Puller::"<<__func__ << " bad peer tips";
            state = PullerState::Done;
            return false;
        }

        if(my_tips.IsBehind(others_tips))
        {
            uint32_t num_eb, num_mb;
            uint64_t num_rb;
            my_tips.ComputeNumberBlocksBehind(others_tips, num_eb, num_mb, num_rb);
            num_blocks_to_download = num_eb + num_mb + num_rb;

            state = PullerState::Epoch;
            final_ep_number = std::max(
                    my_tips.GetLatestEpochNumber(),
                    others_tips.GetLatestEpochNumber());
            CreateMorePulls();
            LOG_DEBUG(log) << "Puller::" << __func__
                           << " I am behind, current # of pull=" << waiting_pulls.size()
                           << " to_download=" << num_blocks_to_download
                           << " eb=" << num_eb
                           << " mb=" << num_mb
                           << " rb=" << num_rb;
            return false;
        }
        else
        {
            state = PullerState::Done;
            LOG_DEBUG(log) <<"Puller::"<< __func__ << " I am not behind.";
            return true;
        }
    }

    PullPtr Puller::GetPull()
    {
        std::lock_guard<std::mutex> lck (mtx);
        if(waiting_pulls.empty())
            return nullptr;
        else
        {
            auto pull = waiting_pulls.front();
            auto insert_res = ongoing_pulls.insert(pull);
            assert(insert_res.second == true);
            waiting_pulls.pop_front();
            return pull;
        }
    }

    bool Puller::AllDone()
    {
        std::lock_guard<std::mutex> lck (mtx);
        LOG_INFO(log) << "Puller::"<<__func__ << ": done=" << (state == PullerState::Done);
        return state == PullerState::Done;
    }

    size_t Puller::GetNumWaitingPulls()
    {
        std::lock_guard<std::mutex> lck (mtx);
        return waiting_pulls.size();
    }

    bool Puller::ReduceNumBlockToDownload()
    {
        if(--num_blocks_to_download == 0)
        {
            LOG_TRACE(log) << "Puller::"<< __func__ << " num_blocks_to_download == 0;"
                                                       " waiting_pulls size: " << waiting_pulls.size()
                                                    << " ongoing_pulls size: " << ongoing_pulls.size();
            assert(waiting_pulls.empty());
            assert(ongoing_pulls.empty());
            state = PullerState::Done;
            return true;
        }else{
            return false;
        }
    }

    PullStatus Puller::EBReceived(PullPtr pull, EBPtr block)
    {
        LOG_TRACE(log) << "Puller::"<<__func__
                << " tip: " << block->CreateTip().to_string()
                << " block->previous: " << block->previous.to_string()
                << " pull->prev_hash: " << pull->prev_hash.to_string();

        assert(state==PullerState::Epoch && working_epoch.eb == nullptr);
        bool good_block = block->previous == pull->prev_hash &&
                block_cache.AddEpochBlock(block) != logos::IBlockCache::add_result::FAILED;

        std::lock_guard<std::mutex> lck (mtx);
        ongoing_pulls.erase(pull);
        if(good_block)
        {
            if(ReduceNumBlockToDownload())
            {
                return PullStatus::Done;
            }
            //TODO Peng: move my_tips forward
//            bool eb_processed = !block_cache.IsBlockCached(block->Hash());
//            if(eb_processed)
//            {
//                UpdateMyEBTip(block);
//                state = PullerState::Epoch;
//                LOG_INFO(log) << "Puller::EBReceived: processed an epoch "
//                              << working_epoch.epoch_num;
//            } else {
                working_epoch.eb = block;
                state = PullerState::Micro;
//            }

            CreateMorePulls();
            return PullStatus::Done;
        }
        else
        {
            waiting_pulls.push_front(pull);
            return PullStatus::DisconnectSender;
        }
    }

    PullStatus Puller::MBReceived(PullPtr pull, MBPtr block)
    {
        LOG_TRACE(log) << "Puller::"<<__func__
                << " tip: " << block->CreateTip().to_string()
                << " block->previous: " << block->previous.to_string()
                << " pull->prev_hash: " << pull->prev_hash.to_string();

        assert(state==PullerState::Micro);
        bool good_block = block->previous == pull->prev_hash &&
                block_cache.AddMicroBlock(block) != logos::IBlockCache::add_result::FAILED;

        std::lock_guard<std::mutex> lck (mtx);
        ongoing_pulls.erase(pull);
        if(good_block)
        {
            if(ReduceNumBlockToDownload())
            {
                return PullStatus::Done;
            }

            //TODO Peng: move my_tips forward
            // check MB in DB

            state = PullerState::Batch;
            if(!working_epoch.two_mbps)
            {
                assert(working_epoch.cur_mbp.mb == nullptr);
                working_epoch.cur_mbp.mb = block;
            }
            else
            {
                assert(working_epoch.next_mbp.mb == nullptr);
                working_epoch.next_mbp.mb = block;
            }

            LOG_TRACE(log) << "Puller::"<<__func__ << MBRequestTips_to_string(*block);

            CreateMorePulls();
            return PullStatus::Done;
        }
        else
        {
            waiting_pulls.push_front(pull);
            return PullStatus::DisconnectSender;
        }
    }

    PullStatus Puller::BSBReceived(PullPtr pull, BSBPtr block, bool last_block)
    {
        LOG_TRACE(log) << "Puller::"<<__func__
                << " tip: " << block->CreateTip().to_string()
                << " block->previous: " << block->previous.to_string()
                << " pull->prev_hash: " << pull->prev_hash.to_string();

        assert(state==PullerState::Batch || state==PullerState::Batch_No_MB ||
               state==PullerState::Batch_No_MB_Next_Epoch);
        bool good_block = block->previous == pull->prev_hash &&
                block->primary_delegate < NUM_DELEGATES &&
                block_cache.AddRequestBlock(block) != logos::IBlockCache::add_result::FAILED;

        auto digest(block->Hash());
        std::lock_guard<std::mutex> lck (mtx);
        if(good_block)
        {
            bool pull_done = digest == pull->target;
            if(pull_done)
            {
                ongoing_pulls.erase(pull);
            }

            if(ReduceNumBlockToDownload())
            {
                return PullStatus::Done;
            }

            pull->prev_hash = digest;
            /*
             * ok to update my bsb tips which is a bootstrap internal state
             * note the block is in Cache in case it cannot be stored yet
             *
             * note the definition of tip changed from "tip in DB" to "tip in DB or Cache",
             * to ease the pull generation, we still keep the logical order, i.e. BSB -> MB -> EB
             */
            UpdateMyBSBTip(block);

            if(pull_done)
            {
                LOG_TRACE(log) << "Puller::BSBReceived: one pull request done";

                auto & working_mbp = working_epoch.two_mbps?
                                     working_epoch.next_mbp : working_epoch.cur_mbp;
                assert(working_mbp.bsb_targets.find(digest) != working_mbp.bsb_targets.end());
                working_mbp.bsb_targets.erase(digest);

                if(working_mbp.bsb_targets.empty())//all BSBs and MB (if have one) in block_cache now
                {
                    //check before go to next micro period
                    CheckMicroProgressAndCreateMorePulls();
                }
                //else: nothing to do
                return PullStatus::Done;
            }
            else
            {
                if(last_block)
                {
                    LOG_INFO(log) << "Puller::BSBReceived: sender doesn't have all we need";
                    ongoing_pulls.erase(pull);
                    waiting_pulls.push_front(pull);
                    return PullStatus::DisconnectSender;
                }
                else
                    return PullStatus::Continue;
            }
        }
        else
        {
            LOG_INFO(log) << "Puller::BSBReceived: bad block";
            ongoing_pulls.erase(pull);
            waiting_pulls.push_front(pull);
            return PullStatus::BlackListSender;
        }
    }

    void Puller::PullFailed(PullPtr pull)
    {
        LOG_TRACE(log) << "Puller::"<<__func__;
        std::lock_guard<std::mutex> lck (mtx);
        ongoing_pulls.erase(pull);
        waiting_pulls.push_front(pull);
    }

    bool Puller::GetTipsets(TipSet &my, TipSet &others, uint8_t &mb_Qed, uint8_t &eb_Qed)
    {
        LOG_TRACE(log) <<"Puller::"<< __func__;
        std::lock_guard<std::mutex> lck (mtx);
        if(!inited)
        {
            LOG_DEBUG(log) <<"Puller::"<< __func__ << " not inited";
            return false;
        }

        my = my_tips;
        others = others_tips;

        if (working_epoch.two_mbps)
            mb_Qed = 2;
        else if (working_epoch.cur_mbp.mb != nullptr)
            mb_Qed = 1;
        else
            mb_Qed = 0;

        if (working_epoch.eb != nullptr)
            eb_Qed = 1;
        else
            eb_Qed = 0;

        return true;
    }


    void Puller::CheckMicroProgressAndCreateMorePulls(bool wakeup)
    {
        LOG_TRACE(log) << "Puller::"<<__func__;
        /*
         * step 1: reduce two mbps to one mbp
         */
        assert(working_epoch.cur_mbp.bsb_targets.empty());
        assert(wakeup ? !working_epoch.two_mbps : true);

        if(working_epoch.two_mbps)
        {
            LOG_TRACE(log) << "Puller::" << __func__ << ": two_mbps {";

            assert(working_epoch.cur_mbp.mb != nullptr);
            assert(working_epoch.next_mbp.bsb_targets.empty());

            auto digest(working_epoch.cur_mbp.mb->Hash());
            auto reduce_mps = [](Puller *this_ptr, const BlockHash &digest) -> bool
            {
                bool mb_processed = !this_ptr->block_cache.IsBlockCached(digest);
                if (mb_processed)
                {
                    this_ptr->UpdateMyMBTip(this_ptr->working_epoch.cur_mbp.mb);
                    this_ptr->working_epoch.cur_mbp = this_ptr->working_epoch.next_mbp;
                    this_ptr->working_epoch.two_mbps = false;
                    this_ptr->working_epoch.next_mbp.Clean();
                }
                return mb_processed;
            };

            if (!reduce_mps(this, digest))
            {
                std::chrono::steady_clock::time_point to = std::chrono::steady_clock::now() +
                                                           std::chrono::milliseconds(BLOCK_CACHE_TIMEOUT_MS);
                std::weak_ptr<Puller> this_w(shared_from_this());
                LOG_INFO(log) << "Puller::CheckMicroProgress will delay reduce_mps "
                              << digest.to_string();
                alarm.add(to, [reduce_mps, this_w, digest]()
                {
                    if (auto puller_l = this_w.lock())
                    {
                        LOG_INFO(puller_l->log) << "Puller::CheckMicroProgress delayed reduce_mps "
                                                << digest.to_string();
                        if (reduce_mps(puller_l.get(), digest))
                        {
                            puller_l->CheckMicroProgressAndCreateMorePulls(true);
                        }
                        else
                        {
                            /*
                             * note that in case of two mbps, if the peer feed us bad tips,
                             * we could stuck. so we CANNOT LOG_FATAL and trace_and_halt.
                             * we LOG_ERROR and terminate this bootstrap attempt.
                             */
                            LOG_ERROR(puller_l->log) << "Puller::CheckMicroProgress: pulled two MB periods,"
                                                     << " but first MB has not been processed. Give up."
                                                     << " epoch_num=" << puller_l->working_epoch.epoch_num
                                                     << " first MB hash=" << digest.to_string();
                            puller_l->waiting_pulls.clear();
                            puller_l->state = PullerState::Done;
                            return;
                        }
                    }
                });
                return;
            }
            else
            {
                LOG_TRACE(log) << "Puller::" << __func__ << ": two_mbps }";
                //continue to one mbp case
            }
        }

        /*
         * step 2: check progress in case cur_mbp has a mb
         */
        if(working_epoch.cur_mbp.mb != nullptr)
        {
            LOG_TRACE(log) << "Puller::"<<__func__<< ": one mbp {";
            auto digest(working_epoch.cur_mbp.mb->Hash());
            bool mb_processed = !block_cache.IsBlockCached(digest);
            if(mb_processed)
            {
                UpdateMyMBTip(working_epoch.cur_mbp.mb);
                if(working_epoch.cur_mbp.mb->last_micro_block)
                {
                    if(working_epoch.eb != nullptr)
                    {
                        auto eb_digest(working_epoch.eb->Hash());
                        auto reduce_ep = [](Puller *this_ptr, const BlockHash &eb_digest) -> bool
                        {
                            bool eb_processed = ! this_ptr->block_cache.IsBlockCached(eb_digest);
                            if (eb_processed)
                            {
                                this_ptr->UpdateMyEBTip(this_ptr->working_epoch.eb);
                                LOG_INFO(this_ptr->log) << "Puller::CheckMicroProgress: processed an epoch "
                                                        << this_ptr->working_epoch.epoch_num;
                            }
                            return eb_processed;
                        };

                        if (!reduce_ep(this, eb_digest))
                        {
                            std::chrono::steady_clock::time_point to = std::chrono::steady_clock::now() +
                                                                       std::chrono::milliseconds(BLOCK_CACHE_TIMEOUT_MS);
                            std::weak_ptr<Puller> this_w(shared_from_this());
                            LOG_INFO(log) << "Puller::CheckMicroProgress will delay reduce_ep "
                                          << eb_digest.to_string();
                            alarm.add(to, [reduce_ep, this_w, eb_digest]()
                            {
                                if (auto puller_l = this_w.lock())
                                {
                                    LOG_INFO(puller_l->log) << "Puller::CheckMicroProgress delayed reduce_ep "
                                                            << eb_digest.to_string();
                                    if (reduce_ep(puller_l.get(), eb_digest))
                                    {
                                        puller_l->state = PullerState::Epoch;
                                        puller_l->working_epoch.cur_mbp.Clean();
                                        puller_l->CreateMorePulls();
                                        puller_l->attempt->wakeup();
                                    }
                                    else
                                    {
                                        LOG_ERROR(puller_l->log) << "Puller::CheckMicroProgress: "
                                                                 << "cannot process epoch block after last micro block "
                                                                 << puller_l->working_epoch.epoch_num;
                                        puller_l->waiting_pulls.clear();
                                        puller_l->state = PullerState::Done;
                                        return;
                                    }
                                }
                            });
                            return;
                        }
                        else
                        {
                            LOG_TRACE(log) << "Puller::" << __func__ << ": one mbp }";
                        }
                    }
                    else
                    {
                        assert(working_epoch.epoch_num+1 == final_ep_number
                                || working_epoch.epoch_num == final_ep_number);
                        LOG_INFO(log) << "Puller::CheckMicroProgress: have last MB but not EB "
                                    << working_epoch.epoch_num;
                    }
                    state = PullerState::Epoch;
                }
                else
                {
                    state = PullerState::Micro;
                }
                working_epoch.cur_mbp.Clean();
            }
            else
            {
                working_epoch.two_mbps = true;
                state = PullerState::Micro;
            }
        }

        CreateMorePulls();
        if(wakeup)
        {
            attempt->wakeup();
        }
        LOG_TRACE(log) << "Puller::"<<__func__ << ": state=" << (int)state;
    }

    void Puller::UpdateMyBSBTip(BSBPtr block)
    {
        LOG_TRACE(log) << "Puller::"<<__func__ << " - block JSON representation: " << block->ToJson();
        auto d_idx = block->primary_delegate;
        assert(d_idx < NUM_DELEGATES);
        LOG_TRACE(log) << "Puller::" << __func__
                       << " block=" << block->ToJson()
                       << " my_tips.bsb_vec[d_idx]=" << my_tips.bsb_vec[d_idx].to_string()
                       << " my_tips.bsb_vec_new_epoch[d_idx]=" << my_tips.bsb_vec_new_epoch[d_idx].to_string();

        BlockHash digest = block->Hash();
        //try old epoch
        if(my_tips.bsb_vec[d_idx].digest == block->previous)
        {
            my_tips.bsb_vec[d_idx] = block->CreateTip();
            if(! (my_tips.bsb_vec[d_idx] < my_tips.bsb_vec_new_epoch[d_idx]))
            {
                my_tips.bsb_vec_new_epoch[d_idx] = Tip();
            }
        }
        else if(my_tips.bsb_vec_new_epoch[d_idx].digest == block->previous)
        {
            my_tips.bsb_vec_new_epoch[d_idx] = block->CreateTip();
        }
        else
        {
            LOG_ERROR(log) << "Puller::UpdateMyBSBTip, cannot find previous";
            assert(false);
        }
    }

    void Puller::UpdateMyMBTip(MBPtr block)
    {
        LOG_TRACE(log) << "Puller::"<<__func__;
        assert(my_tips.mb.digest == block->previous);
        my_tips.mb = block->CreateTip();
    }

    void Puller::UpdateMyEBTip(EBPtr block)
    {
        LOG_TRACE(log) << "Puller::"<<__func__;
        assert(my_tips.eb.digest == block->previous);
        my_tips.eb = block->CreateTip();
    }

    void Puller::CreateMorePulls()
    {
        LOG_TRACE(log) << "Puller::"<<__func__<< " state="<<(uint)state;
        // should be called with mtx locked
        // should be called only when both waiting_pulls and ongoing_pulls are empty
        assert(waiting_pulls.empty() && ongoing_pulls.empty());

        switch (state)
        {
            case PullerState::Epoch:
            {
                working_epoch = {my_tips.eb.epoch+1};
                if(my_tips.eb < others_tips.eb)
                {
                    waiting_pulls.push_back(std::make_shared<PullRequest>(
                            ConsensusType::Epoch,
                            working_epoch.epoch_num,
                            my_tips.eb.digest));
                    return;
                }else{
                    state = PullerState::Micro;
                    CreateMorePulls();
                }
                break;
            }
            case PullerState::Micro:
            {
                assert(working_epoch.cur_mbp.bsb_targets.empty());

                auto mb_tip = working_epoch.two_mbps?
                        working_epoch.cur_mbp.mb->CreateTip():my_tips.mb;

                if(mb_tip < others_tips.mb)
                {
                    waiting_pulls.push_back(std::make_shared<PullRequest>(
                            ConsensusType::MicroBlock,
                            working_epoch.epoch_num,
                            mb_tip.digest));
                    return;
                }else{
                    state = PullerState::Batch_No_MB;
                    CreateMorePulls();
#if 1 //TODO remove after integration tests
                    {
                        auto & working_mbp = working_epoch.two_mbps?
                                             working_epoch.next_mbp : working_epoch.cur_mbp;
                        assert(working_mbp.mb == nullptr);
                    }
#endif
                }
                break;
            }
            case PullerState::Batch:
            {
                bool added_pulls = false;
                // if pulled a MB, use it to set up pulls
                auto & working_mbp = working_epoch.two_mbps?
                                     working_epoch.next_mbp : working_epoch.cur_mbp;
                assert(working_mbp.mb != nullptr);
                ////TODO remove after integration tests
//                assert(my_tips.mb.sqn == working_mbp.mb->sequence -1 ||
//                        ((my_tips.mb.epoch == working_mbp.mb->epoch_number -1) &&
//                                (working_mbp.mb->sequence == 0)));
                assert(working_mbp.bsb_targets.empty());

                for(uint i = 0; i < NUM_DELEGATES; ++i)
                {
                    if(my_tips.bsb_vec[i] < working_mbp.mb->tips[i])
                    {
                        waiting_pulls.push_back(std::make_shared<PullRequest>(
                                ConsensusType::Request,
                                working_epoch.epoch_num,
                                my_tips.bsb_vec[i].digest,
                                working_mbp.mb->tips[i].digest));

                        working_mbp.bsb_targets.insert(working_mbp.mb->tips[i].digest);
                        added_pulls = true;
                    }
                }
                if(!added_pulls)
                {
                    //no bsb to pull, check before go to next micro period
                    CheckMicroProgressAndCreateMorePulls();
                }
                break;
            }
            case PullerState::Batch_No_MB:
            {
                bool added_pulls = false;
                /*
                 * since we don't have any MB, we are at the end of the bootstrap
                 * we consider the two cases that we need to pull BSBs
                 * (1) my BSB tips are behind in the working epoch
                 */
                assert(working_epoch.eb == nullptr);
                auto &working_mbp = working_epoch.two_mbps ?
                                    working_epoch.next_mbp : working_epoch.cur_mbp;
                for (uint i = 0; i < NUM_DELEGATES; ++i)
                {
                    if (my_tips.bsb_vec[i] < others_tips.bsb_vec[i])
                    {
                        waiting_pulls.push_back(std::make_shared<PullRequest>(
                                ConsensusType::Request,
                                working_epoch.epoch_num,
                                my_tips.bsb_vec[i].digest,
                                others_tips.bsb_vec[i].digest));

                        LOG_TRACE(log) << "Puller::" << __func__ << " added:"
                                       << waiting_pulls.back()->to_string();

                        working_mbp.bsb_targets.insert(others_tips.bsb_vec[i].digest);
                        added_pulls = true;
                    }
                }
                if (added_pulls)
                {
                    LOG_TRACE(log) << "Puller::"<<__func__<< " return Batch_No_MB";
                    return;
                }
                else
                {
                    //check before go to next epoch
                    state = PullerState::Batch_No_MB_Next_Epoch;
                    CheckMicroProgressAndCreateMorePulls();
                }
            }
            case PullerState::Batch_No_MB_Next_Epoch:
            {
                /*
                 * (2) my BSB tips in bsb_vec_new_epoch is behind.
                 * Note that at this point, the current epoch's last MB and EB are not
                 * (created)received yet. No more to do for the current epoch.
                 * So for (2), we move to the new epoch
                 */
                bool added_pulls = false;
                working_epoch = {working_epoch.epoch_num+1};
                for(uint i = 0; i < NUM_DELEGATES; ++i)
                {
                    if(my_tips.bsb_vec_new_epoch[i] < others_tips.bsb_vec_new_epoch[i])
                    {
                        waiting_pulls.push_back(std::make_shared<PullRequest>(
                                ConsensusType::Request,
                                working_epoch.epoch_num,
                                my_tips.bsb_vec_new_epoch[i].digest,
                                others_tips.bsb_vec_new_epoch[i].digest));

                        working_epoch.cur_mbp.bsb_targets.insert(others_tips.bsb_vec_new_epoch[i].digest);
                        added_pulls = true;
                    }
                }

                if(!added_pulls)
                {
                    state = PullerState::Done;
                }
                LOG_TRACE(log) << "Puller::"<<__func__
                               << " working_epoch.epoch_num " << working_epoch.epoch_num
                               << " final_ep_number " << final_ep_number;
                assert(working_epoch.epoch_num == final_ep_number ||
                        working_epoch.epoch_num-1 == final_ep_number ||
                        working_epoch.epoch_num-2 == final_ep_number);
                break;
            }
            case PullerState::Done:
            default:
                break;
        }
    }

    ///////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////

#ifndef CONSENSUS_BLOCK_DB_RAW

    PullRequestHandler::PullRequestHandler(PullRequest request, Store & store)
    : request(request)
    , store(store)
    , next(0)
    {
        LOG_TRACE(log) << "PullRequestHandler::"<<__func__;
        //setup next
        if( ! request.prev_hash.is_zero())
        {
            std::vector<uint8_t> buf;
            GetBlock(request.prev_hash, buf);
        }
        else
        {
            if(request.block_type == ConsensusType::Request &&
                    ! request.target.is_zero())
            {
                TraceToEpochBegin();
            }
        }
    }

    uint32_t PullRequestHandler::GetBlock(BlockHash & hash, std::vector<uint8_t> & buf)
    {
        switch(request.block_type)
        {
        case ConsensusType::Request:
        {
            ApprovedRB block;
            if(store.request_block_get(hash, block))
            {
                next = 0;
                return 0;
            }
            else
            {
                next = block.next;
                buf.resize(PullResponseReserveSize);
                logos::vectorstream stream(buf);
                return block.Serialize(stream, true, true);
            }
        }
        case ConsensusType::MicroBlock:
        {
            ApprovedMB block;
            if(store.micro_block_get(hash, block))
            {
                next = 0;
                return 0;
            }
            else
            {
                next = block.next;
                buf.resize(PullResponseReserveSize);
                logos::vectorstream stream(buf);
                return block.Serialize(stream, true, true);
            }
        }
        case ConsensusType::Epoch:
        {
            ApprovedEB block;
            if(store.epoch_get(hash, block))
            {
                next = 0;
                return 0;
            }
            else
            {
                next = block.next;
                buf.resize(PullResponseReserveSize);
                logos::vectorstream stream(buf);
                return block.Serialize(stream, true, true);
            }
        }
        default:
            return 0;
        }
    }

    void PullRequestHandler::TraceToEpochBegin()
    {
        LOG_TRACE(log) << "PullRequestHandler::"<<__func__;
        BlockHash cur(request.target);
        ApprovedRB block;
        for(;;)
        {
            if(store.request_block_get(cur, block))
            {
                next = 0;
                return;
            }

            if(block.previous.is_zero())
            {
                next = cur;
                return;
            }
            if(block.epoch_number!=request.epoch_num)
            {
                return;
            }
            next = cur;
            cur = block.previous;
        }
    }

    bool PullRequestHandler::GetNextSerializedResponse(std::vector<uint8_t> & buf)
    {
        LOG_TRACE(log) << "PullRequestHandler::"<<__func__;
        assert(buf.empty());

        uint32_t block_size = 0;
        auto cur(next);
        if(!cur.is_zero())
        {
            block_size = GetBlock(cur, buf);
        }

        auto status = PullResponseStatus::NoBlock;
        if(block_size > 0)//have block
        {
            LOG_TRACE(log) << "PullRequestHandler::"<<__func__
                    << " CT=" << ConsensusToName(request.block_type)
                    << " cur=" << cur.to_string()
                    << " next=" << next.to_string()
                    << " target=" << request.target.to_string();

            if(request.block_type == ConsensusType::MicroBlock ||
                    request.block_type == ConsensusType::Epoch ||
                    cur == request.target)
            {
                status = PullResponseStatus::LastBlock;
                next = 0;
            }
            else
            {
                if(next == 0)
                {
                    status = PullResponseStatus::LastBlock;
                }
                else
                {
                    status = PullResponseStatus::MoreBlock;
                }
            }
        }
        auto ps = PullResponseSerializedLeadingFields(request.block_type, status, block_size, buf);

        LOG_TRACE(log) << "PullRequestHandler::"<<__func__
                <<" type=" << ConsensusToName(request.block_type)
                <<" status=" <<PullResponseStatusToName(status)
                <<" packet size="<<ps
                <<" block size="<<block_size
                <<" buf size="<<buf.size();
        assert(ps==buf.size());

        return status == PullResponseStatus::MoreBlock;
    }

#else

    PullRequestHandler::PullRequestHandler(PullRequest request, Store & store)
    : request(request)
    , store(store)
    , next(0)
    {
        LOG_TRACE(log) << "PullRequestHandler::"<<__func__;
        std::vector<uint8_t> buf;
        if( ! request.prev_hash.is_zero())
        {
            uint32_t block_size = GetBlock(request.prev_hash, buf);
            if(block_size > 0)//have block
            {
                memcpy (next.data(),
                        buf.data() + PullResponseReserveSize + block_size - HASH_SIZE,
                        HASH_SIZE);
            }
        }
        else
        {
            if(request.block_type == ConsensusType::Request &&
                    ! request.target.is_zero())
            {
                TraceToEpochBegin();
            }
        }
    }

    uint32_t PullRequestHandler::GetBlock(BlockHash & hash, std::vector<uint8_t> & buf)
    {
        LOG_TRACE(log) << "PullRequestHandler::"<<__func__ <<" hash="<<hash.to_string();
        if (request.block_type == ConsensusType::Request ||
                request.block_type == ConsensusType::MicroBlock ||
                request.block_type == ConsensusType::Epoch)
        {
            return store.consensus_block_get_raw(hash,
                    request.block_type,
                    PullResponseReserveSize,
                    buf);
        }
        return 0;
    }

    void PullRequestHandler::TraceToEpochBegin()
    {
        LOG_TRACE(log) << "PullRequestHandler::"<<__func__;
        BlockHash cur(request.target);
        ApprovedRB block;
        for(;;)
        {
            if(store.request_block_get(cur, block))
            {
                next = 0;
                return;
            }

            LOG_TRACE(log) << "PullRequestHandler::"<<__func__ << " work on block with tip: "
                    << block.CreateTip().to_string();

            if(block.previous.is_zero())
            {
                next = cur;
                return;
            }
            if(block.epoch_number!=request.epoch_num)
            {
                return;
            }
            next = cur;
            cur = block.previous;
        }
    }

    bool PullRequestHandler::GetNextSerializedResponse(std::vector<uint8_t> & buf)
    {
        LOG_TRACE(log) << "PullRequestHandler::"<<__func__;
        assert(buf.empty());

        uint32_t block_size = 0;
        if(!next.is_zero())
        {
            block_size = GetBlock(next, buf);
        }

        auto status = PullResponseStatus::NoBlock;
        if(block_size > 0)//have block
        {
            if(request.block_type == ConsensusType::MicroBlock ||
                    request.block_type == ConsensusType::Epoch ||
                    next == request.target)
            {
                status = PullResponseStatus::LastBlock;
                next = 0;
            }
            else
            {
                memcpy (next.data(),
                        buf.data() + PullResponseReserveSize + block_size - HASH_SIZE,
                        HASH_SIZE);
                if(next == 0)
                {
                    status = PullResponseStatus::LastBlock;
                }
                else
                {
                    status = PullResponseStatus::MoreBlock;
                }
            }
        }
        auto ps = PullResponseSerializedLeadingFields(request.block_type, status, block_size, buf);

        LOG_TRACE(log) << "PullRequestHandler::"<<__func__
                <<" status=" <<(uint)status
                <<" packet size="<<ps
                <<" block size="<<block_size
                <<" buf size="<<buf.size();
        assert(ps==buf.size());

        return status == PullResponseStatus::MoreBlock;
    }
#endif

}//namespace
