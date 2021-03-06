/// @file
/// This file contains declaration of Epoch related validation and persistence

#include <logos/consensus/persistence/epoch/epoch_persistence.hpp>
#include <logos/microblock/microblock_handler.hpp>
#include <logos/rewards/epoch_rewards_manager.hpp>
#include <logos/staking/voting_power_manager.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/epoch/epoch_handler.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
#include <logos/staking/staking_manager.hpp>
#include <logos/lib/trace.hpp>
#include <logos/node/websocket.hpp>

#include <numeric>

PersistenceManager<ECT>::PersistenceManager(Store & store,
                                            ReservationsPtr,
                                            Milliseconds clock_drift)
    : Persistence(store, clock_drift)
{}

bool
PersistenceManager<ECT>::Validate(
    const PrePrepare & epoch,
    ValidationStatus * status)
{
    using namespace logos;
    ApprovedEB previous_epoch;
    bool previous_epoch_on = false;

    if (!status || status->progress < EVP_EPOCH_TIP)
    {
        Tip epoch_tip;

        if (_store.epoch_get(epoch.previous, previous_epoch))
        {
            LOG_ERROR(_log) << "PersistenceManager<ECT>::Validate failed to get epoch: " <<
                            epoch.previous.to_string();
            UpdateStatusReason(status, process_result::gap_previous);
            return false;
        }

        previous_epoch_on = true;

        if (_store.epoch_tip_get(epoch_tip))
        {
            LOG_FATAL(_log) << "PersistenceManager<ECT>::Validate failed to get epoch tip";
            trace_and_halt();
        }

        // verify epoch number = previous + 1
        if (epoch.epoch_number != (previous_epoch.epoch_number + 1)
                || epoch_tip.digest != epoch.previous
                || epoch_tip.epoch != previous_epoch.epoch_number
                /* || epoch_tip.sqn != previous_epoch.epoch_number */)
        {
            LOG_ERROR(_log) << "PersistenceManager<ECT>::Validate account invalid epoch number " <<
                            epoch.epoch_number << " " << previous_epoch.epoch_number;
            UpdateStatusReason(status, process_result::block_position);
            return false;
        }

        if (epoch.total_RBs != previous_epoch.total_RBs + EpochHandler::ComputeNumRBs(_store, epoch.epoch_number))
        {
            LOG_WARN(_log) << "PersistenceManager<ECT>::Validate total_RBs is wrong"
                            << " actual=" << epoch.total_RBs
                            << " expect=" << previous_epoch.total_RBs + EpochHandler::ComputeNumRBs(_store, epoch.epoch_number);
            UpdateStatusReason(status, process_result::invalid_number_blocks);
            return false;
        }

        if (status)
            status->progress = EVP_EPOCH_TIP;
    }

    if (!status || status->progress < EVP_MICRO_TIP)
    {
        ApprovedMB last_micro_block;
        Tip micro_block_tip;

        if (_store.micro_block_get(epoch.micro_block_tip.digest, last_micro_block))
        {
            LOG_ERROR(_log) << "PersistenceManager<ECT>::Validate failed to get last microblock: " <<
                            epoch.micro_block_tip.digest.to_string();
            UpdateStatusReason(status, process_result::invalid_tip);
            return false;
        }

        if (!last_micro_block.last_micro_block) {
            LOG_ERROR(_log) << "PersistenceManager<ECT>::Validate failed to verify last microblock: " <<
                            epoch.micro_block_tip.digest.to_string();
            UpdateStatusReason(status, process_result::invalid_request);
            return false;
        }

        // verify microblock tip exists
        if (_store.micro_block_tip_get(micro_block_tip))
        {
            LOG_FATAL(_log) << "PersistenceManager<ECT>::Validate failed to get microblock tip";
            trace_and_halt();
        }

        if (epoch.micro_block_tip != micro_block_tip
                || micro_block_tip.epoch != epoch.epoch_number)
        {
            LOG_ERROR(_log) << "PersistenceManager<ECT>::Validate previous micro block doesn't exist " <<
                            epoch.micro_block_tip.to_string() << " " << micro_block_tip.to_string();
            UpdateStatusReason(status, process_result::invalid_tip);
            return false;
        }

        if (!previous_epoch_on && _store.epoch_get(epoch.previous, previous_epoch))
        {
            LOG_ERROR(_log) << "PersistenceManager<ECT>::Validate failed to get existed epoch: " <<
                            epoch.previous.to_string();
            trace_and_halt();
        }

        uint64_t total_RBs = 0;
        for (int del = 0; del < NUM_DELEGATES; ++del)
        {
            if (!last_micro_block.tips[del].digest.is_zero())
                total_RBs += last_micro_block.tips[del].sqn + 1;
        }
        if (epoch.total_RBs != previous_epoch.total_RBs + total_RBs)
        {
            LOG_ERROR(_log) << "PersistenceManager::VerifyEpoch number of batch blocks doesn't match in block: "
                            << " hash " << epoch.Hash().to_string()
                            << " number in block received=" << epoch.total_RBs
                            << " locally expect=" << previous_epoch.total_RBs + total_RBs;
            UpdateStatusReason(status, process_result::invalid_number_blocks);
            return false;
        }

        if (status)
            status->progress = EVP_MICRO_TIP;
    }

    if (!status || status->progress < EVP_VOTING)
    {
        if (epoch.primary_delegate >= NUM_DELEGATES)
        {
            UpdateStatusReason(status, process_result::invalid_request);
            LOG_ERROR(_log) << "PersistenceManager<ECT>::Validate primary index out of range " << (int) epoch.primary_delegate;
            return false;
        }

        EpochVotingManager voting_mgr(_store);
        //epoch block has epoch_number 1 less than current epoch, so +1
        if (!voting_mgr.ValidateEpochDelegates(epoch.delegates, epoch.epoch_number + 1))
        {
            LOG_ERROR(_log) << "PersistenceManager<ECT>::Validate invalid delegates ";
            UpdateStatusReason(status, process_result::not_delegate);
            return false;
        }

        if (status)
            status->progress = EVP_VOTING;
    }

    if (!status || status->progress < EVP_END)
    {
        Amount    transaction_fee_pool_local = 0;
        if(EpochRewardsManager::GetInstance()->GetFeePool(epoch.epoch_number, transaction_fee_pool_local))
        {
            LOG_WARN(_log) << "PersistenceManager<ECT>::Validate failed to get fee pool for epoch: "
                            << epoch.epoch_number;
        }
        if(transaction_fee_pool_local != epoch.transaction_fee_pool)
        {
            LOG_ERROR(_log) << "PersistenceManager<ECT>::Validate fee pool mismatch,"
                            << " local=" << transaction_fee_pool_local.to_string_dec()
                            << " other=" << epoch.transaction_fee_pool.to_string_dec();
            UpdateStatusReason(status, process_result::invalid_fee);
            return false;
        }

        if (status)
            status->progress = EVP_END;
    }

    return true;
}

void
PersistenceManager<ECT>::ApplyUpdates(
    const ApprovedEB & block,
    uint8_t)
{
    LOG_INFO(_log) << "Applying updates for Epoch";
    logos::transaction transaction(_store.environment, nullptr, true);

    // See comments in request_persistence.cpp
    if (BlockExists(block))
    {
        LOG_DEBUG(_log) << "PersistenceManager<ECT>::ApplyUpdates - epoch already exists, ignoring";
        return;
    }

    BlockHash epoch_hash = block.Hash();
    bool transition = EpochVotingManager::ENABLE_ELECTIONS;

    UpdateThawing(block, transaction);

    if(_store.epoch_put(block, transaction) || _store.epoch_tip_put(block.CreateTip(), transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager<ECT>::ApplyUpdates failed to store epoch or epoch tip "
                        << epoch_hash.to_string();
        trace_and_halt();
    }

    //The epoch number in the epoch block is one less than the current epoch
    if(transition)
    {
        TransitionNextEpoch(transaction, block.epoch_number+1);
    }

    if(_store.consensus_block_update_next(block.previous, epoch_hash, ConsensusType::Epoch, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager<ECT>::ApplyUpdates failed to get previous block "
                        << block.previous.to_string();
        trace_and_halt();
    }

    logos_global::OnNewBlock<ECT>(block);

    // Link epoch's first request block with previous epoch's last request block
    // starting from epoch 3 (i.e. after Genesis)
    if (block.epoch_number <= GENESIS_EPOCH)
    {
        return;
    }

    BatchTips cur_e_first;
    auto cur_epoch_number (block.epoch_number + 1);
    _store.GetEpochFirstRBs(cur_epoch_number, cur_e_first);

    for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate)
    {
        LinkAndUpdateTips(delegate, cur_epoch_number, cur_e_first[delegate], transaction);
    }

    if(block.transaction_fee_pool > 0)
    {
        ApplyRewards(block, epoch_hash, transaction);
    }

    UpdateGlobalRewards(block, transaction);
}

void
PersistenceManager<ECT>::LinkAndUpdateTips(
    uint8_t delegate,
    uint32_t epoch_number,
    const Tip & first_request_block,
    MDB_txn *transaction)
{
    // Get previous epoch's request block tip
    Tip prev_e_last;
    if (_store.request_tip_get(delegate, epoch_number - 1, prev_e_last))
    {
        LOG_FATAL(_log) << "PersistenceManager<ECT>::LinkAndUpdateTips failed to get request block tip for delegate "
                        << std::to_string(delegate) << " for epoch number " << epoch_number - 1;
        trace_and_halt();
    }

    // Don't connect chains if current epoch doesn't contain a tip yet. See request block persistence for this case
    if (first_request_block.digest.is_zero())
    {
        // Use old request block tip for current epoch
        if (_store.request_tip_put(delegate, epoch_number, prev_e_last, transaction))
        {
            LOG_FATAL(_log) << "PersistenceManager<ECT>::LinkAndUpdateTips failed to put request block tip for delegate "
                            << std::to_string(delegate) << " for epoch number " << epoch_number;
            trace_and_halt();
        }
    }
    else
    {
        // Update `next` of last request block in previous epoch
        if (_store.consensus_block_update_next(prev_e_last.digest, first_request_block.digest, ConsensusType::Request, transaction))
        {
            LOG_FATAL(_log) << "PersistenceManager<ECT>::LinkAndUpdateTips failed to update prev epoch's "
                            << "request block tip for delegate " << std::to_string(delegate);
            trace_and_halt();
        }

        // Update `previous` of first request block in epoch
        if (_store.request_block_update_prev(first_request_block.digest, prev_e_last.digest, transaction))
        {
            LOG_FATAL(_log) << "PersistenceManager<ECT>::LinkAndUpdateTips failed to update current epoch's "
                            << "first request block prev for delegate " << std::to_string(delegate);
            trace_and_halt();
        }
    }

    // Can safely delete old epoch tip because it's either "rolled over" to current epoch or successfully linked
    _store.request_tip_del(delegate, epoch_number - 1, transaction);
}

bool PersistenceManager<ECT>::BlockExists(
    const ApprovedEB & message)
{
    return _store.epoch_exists(message);
}


void PersistenceManager<ECT>::UpdateThawing(ApprovedEB const & block, MDB_txn* txn)
{
    ApprovedEB prev_epoch;
    Tip prev_tip;
    if(_store.epoch_tip_get(prev_tip, txn) || _store.epoch_get(prev_tip.digest, prev_epoch, txn))
    {
        LOG_FATAL(_log) << "PersistenceManager<ECT>::ApplyUpdates - "
                        << "failed to get previous epoch";
        trace_and_halt();
    }

    std::unordered_set<AccountAddress> new_dels;
    for(auto del : block.delegates)
    {
        new_dels.insert(del.account);
    }


    for(auto del : prev_epoch.delegates)
    {
        if(new_dels.find(del.account) == new_dels.end())
        {
            //epoch_number+2 because delegate is retired
            //in the epoch following current epoch
            StakingManager::GetInstance()->SetExpirationOfFrozen(
                    del.account,
                    block.epoch_number+2,
                    txn);
        }
    }

    for(auto del : block.delegates)
    {
        //Mark any funds that began thawing in previous epoch
        //as frozen
        StakingManager::GetInstance()->MarkThawingAsFrozen(
                del.account,
                block.epoch_number,
                txn);
    }
}


void PersistenceManager<ECT>::MarkDelegateElectsAsRemove(MDB_txn* txn)
{
    Tip tip;
    assert(!_store.epoch_tip_get(tip,txn));
    ApprovedEB epoch;
    assert(!_store.epoch_get(tip.digest,epoch,txn));

    for(Delegate& d: epoch.delegates)
    {
        if(d.starting_term)
        {
            assert(!_store.candidate_mark_remove(d.account,txn));
        }
    }
}

void PersistenceManager<ECT>::AddReelectionCandidates(
        uint32_t next_epoch_num,
        MDB_txn* txn)
{
    ApprovedEB epoch;

    auto is_not_extension = [](ApprovedEB& eb)
    {
        return !eb.is_extension;
    };
    bool res = _store.epoch_get_n(3,epoch,txn,is_not_extension);
    assert(!res);

    for(auto& d : epoch.delegates)
    {
        if(d.starting_term)
        {
            RepInfo rep;
            if(!_store.rep_get(d.account,rep,txn))
            {
                std::shared_ptr<Request> req;
                assert(!_store.request_get(rep.candidacy_action_tip,req,txn));
                if(req->type == RequestType::AnnounceCandidacy)
                {
                    auto ac = static_pointer_cast<AnnounceCandidacy>(req); 
                    CandidateInfo candidate(*ac);
                
                    VotingPowerInfo vp_info;
                    res = VotingPowerManager::GetInstance()
                        ->GetVotingPowerInfo(d.account, next_epoch_num, vp_info, txn);
                    if(!res)
                    {
                        LOG_FATAL(_log) << "PersistenceManager<ECT>::AddReelectionCandidates - "
                            << "failed to find voting power info for account = "
                            << d.account.to_string();
                        trace_and_halt();
                    }
                    candidate.cur_stake = vp_info.current.self_stake;
                    candidate.next_stake = vp_info.next.self_stake;
                    assert(!_store.candidate_put(d.account, candidate, txn));
                }
            }
        }
    }
}

void PersistenceManager<ECT>::UpdateRepresentativesDB(MDB_txn* txn)
{
    auto vpm = VotingPowerManager::GetInstance();
    for(auto it = logos::store_iterator(txn, _store.remove_reps_db);
            it != logos::store_iterator(nullptr); ++it)
    {
        auto status (mdb_del(txn, _store.representative_db, it->second, nullptr));
        vpm->TryPrune(it->second.uint256(), txn);
        assert(status == 0);
    }

    _store.clear(_store.remove_reps_db, txn);
}

void PersistenceManager<ECT>::UpdateCandidatesDB(MDB_txn* txn)
{

    for(auto it = logos::store_iterator(txn, _store.remove_candidates_db);
            it != logos::store_iterator(nullptr); ++it)
    {
        auto status (mdb_del(txn, _store.candidacy_db, it->second, nullptr));
        assert(status == 0);
    }

    _store.clear(_store.remove_candidates_db, txn);

    _store.clear(_store.leading_candidates_db,txn);
    _store.leading_candidates_size = 0;
}


void PersistenceManager<ECT>::TransitionCandidatesDBNextEpoch(MDB_txn* txn, uint32_t next_epoch_num)
{
    if(next_epoch_num >= EpochVotingManager::START_ELECTIONS_EPOCH)
    {
        AddReelectionCandidates(next_epoch_num,txn);
    }
    if(next_epoch_num > EpochVotingManager::START_ELECTIONS_EPOCH)
    {
        MarkDelegateElectsAsRemove(txn);
    }
    UpdateCandidatesDB(txn);
}

void PersistenceManager<ECT>::TransitionNextEpoch(MDB_txn* txn, uint32_t next_epoch_num)
{
    TransitionCandidatesDBNextEpoch(txn,next_epoch_num);
    UpdateRepresentativesDB(txn);
}

void PersistenceManager<ECT>::ApplyRewards(const ApprovedEB & block, const BlockHash & hash, MDB_txn * txn)
{
    ApprovedEB prev;

    if(_store.epoch_get(block.previous, prev, txn))
    {
        LOG_FATAL(_log) << "PersistenceManager<ECT>::ApplyRewards - "
                        << "failed to find previous epoch block for epoch number "
                        << block.epoch_number;

        trace_and_halt();
    }

    // Retrieve the antepenultimate epoch to access
    // each delegate's raw stake which determine
    // to the rewards it earns for the current
    // epoch.
    if(_store.epoch_get(prev.previous, prev, txn))
    {
        LOG_FATAL(_log) << "PersistenceManager<ECT>::ApplyRewards - "
                        << "failed to find antepenultimate epoch block for epoch number "
                        << block.epoch_number;

        trace_and_halt();
    }

    // Use this comparison function to sort the delegates
    // according to stake.
    auto stake_cmp = [](const auto & a, const auto & b)
    {
        if(a.raw_stake != b.raw_stake)
        {
            return a.raw_stake > b.raw_stake;
        }
        else
        {
            return Blake2bHash(a).number() > Blake2bHash(b).number();
        }
    };

    std::sort(std::begin(prev.delegates), std::end(prev.delegates), stake_cmp);

    auto acc_stake = [](const auto & a, const auto & b)
    {
        return a + b.raw_stake;
    };

    Amount total_stake = std::accumulate(std::begin(prev.delegates),
                                         std::end(prev.delegates),
                                         Amount{0},
                                         acc_stake);

    auto fee_pool = block.transaction_fee_pool;
    Rational remaining_pool = fee_pool.number();

    // This loop distributes the rewards according to
    // personal stake.
    for(int i = 0; i < NUM_DELEGATES; ++i)
    {
        auto & d = prev.delegates[i];

        if(remaining_pool == 0)
        {
            // TODO: Should never happen in prod.
            //       Only acceptable in test.
            LOG_ERROR(_log) << "PersistenceManager<ECT>::ApplyRewards - "
                            << "Quitting reward distribution early on delegate: "
                            << d.account.to_string();

            break;
        }

        auto earnings = Rational(d.raw_stake.number(), total_stake.number()) * fee_pool.number();
        remaining_pool -= earnings;

        logos::account_info info;
        if(_store.account_get(d.account, info, txn))
        {
            LOG_FATAL(_log) << "PersistenceManager<ECT>::ApplyRewards - "
                            << "failed to find account for delegate.";

            trace_and_halt();
        }

        auto account_put = [&]()
        {
            if(_store.account_put(d.account, info, txn))
            {
                LOG_FATAL(_log) << "PersistenceManager<ECT>::ApplyRewards - "
                                << "Failed to store account: "
                                << d.account.to_string();

                trace_and_halt();
            }
        };

        Reward reward = Reward((earnings.numerator() / earnings.denominator()).convert_to<logos::uint128_t>(),
                               Rational(earnings.numerator() % earnings.denominator(),
                                        earnings.denominator()));

        info.dust += std::get<1>(reward);

        auto deposit_amount = std::get<0>(reward);

        if(info.dust.numerator() >= info.dust.denominator())
        {
            ++deposit_amount;
            --info.dust;
        }

        if(deposit_amount > 0)
        {
            info.SetBalance(info.GetBalance() + deposit_amount, block.epoch_number + 1, txn);

            ReceiveBlock receive(
                /* Previous    */ info.receive_head,
                /* source_hash */ hash,
                /* index       */ i
            );

            info.receive_count++;
            info.receive_head = receive.Hash();
            info.modified = logos::seconds_since_epoch();

            account_put();

            PlaceReceive(receive, block.timestamp, txn);
        }
        else
        {
            account_put();
        }
    }

    EpochRewardsManager::GetInstance()->RemoveFeePool(block.epoch_number, txn);
}

void PersistenceManager<ECT>::UpdateGlobalRewards(const ApprovedEB & block, MDB_txn * txn)
{
    auto reward_manager = EpochRewardsManager::GetInstance();

    if(reward_manager->GlobalRewardsAvailable(block.epoch_number, txn))
    {
        ApprovedEB previous;

        if(_store.epoch_get(block.previous, previous, txn))
        {
            LOG_FATAL(_log) << "PersistenceManager<ECT>::UpdateGlobalRewards failed to retrieve epoch with hash "
                            << block.previous.to_string();
            trace_and_halt();
        }

        Amount new_logos = block.total_supply - previous.total_supply;

        reward_manager->SetGlobalReward(block.epoch_number,
                                        new_logos,
                                        txn);
    }
}
