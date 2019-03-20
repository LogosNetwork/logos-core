#include <logos/blockstore.hpp>
#include <logos/node/common.hpp>
#include <logos/node/node.hpp>
#include <logos/node/utility.hpp>

#include <logos/bootstrap/pull.hpp>
#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/tips.hpp>

namespace Bootstrap
{
	Puller::Puller(IBlockCache & block_cache)//, Store & store)
	: block_cache(block_cache)
//	, store(store)
	{
		LOG_TRACE(log) << "Puller::"<<__func__;
	}

	void Puller::Init(TipSet & my_tips, TipSet & others_tips)
	{
		LOG_TRACE(log) << "Puller::"<<__func__;
		std::lock_guard<std::mutex> lck (mtx);
		this->my_tips = my_tips;
		this->others_tips = others_tips;
		if(my_tips.IsBehind(others_tips))
		{
			state = PullState::Epoch;
			final_ep_number = std::max(
					my_tips.GetLatestEpochNumber(),
					others_tips.GetLatestEpochNumber());
			CreateMorePulls();
		}
		else
		{
			state = PullState::Done;
		}
	}

	PullPtr Puller::GetPull()
	{
		std::lock_guard<std::mutex> lck (mtx);
		auto pull = waiting_pulls.front();
		auto insert_res = ongoing_pulls.insert(pull);
		assert(insert_res.second == true);
		waiting_pulls.pop_front();
		return pull;
	}

	bool Puller::AllDone()
	{
		std::lock_guard<std::mutex> lck (mtx);
		return state == PullState::Done;
	}

	size_t Puller::GetNumWaitingPulls()
	{
		std::lock_guard<std::mutex> lck (mtx);
		return waiting_pulls.size();
	}

	PullStatus Puller::EBReceived(PullPtr pull, EBPtr block)
    {
		LOG_TRACE(log) << "Puller::"<<__func__;
    	assert(state==PullState::Epoch && working_epoch.eb == nullptr);
    	bool good_block = block->previous == pull->prev_hash &&
    			block_cache.AddEB(block);

		std::lock_guard<std::mutex> lck (mtx);
		ongoing_pulls.erase(pull);
    	if(good_block)
    	{
        	working_epoch.eb = block;
        	state = PullState::Micro;
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
		LOG_TRACE(log) << "Puller::"<<__func__;
    	assert(state==PullState::Micro);
    	bool good_block = block->previous == pull->prev_hash &&
    			//block->epoch_number == working_epoch.epoch_num &&
				block_cache.AddMB(block);

		std::lock_guard<std::mutex> lck (mtx);
		ongoing_pulls.erase(pull);
    	if(good_block)
    	{
        	state = PullState::Batch;
        	if(!working_epoch.two_mbps)
        	{
        		assert(working_epoch.cur_mbp.mb == nullptr);
        		working_epoch.cur_mbp.mb == block;
        	}
        	else
        	{
        		assert(working_epoch.next_mbp.mb == nullptr);
        		working_epoch.next_mbp.mb == block;
        	}
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
		LOG_TRACE(log) << "Puller::"<<__func__;
    	assert(state==PullState::Batch || state==PullState::Batch_No_MB);
    	bool good_block = block->previous == pull->prev_hash &&
    			block->epoch_number == working_epoch.epoch_num &&
				block_cache.AddBSB(block);

		auto digest(block->Hash());
		std::lock_guard<std::mutex> lck (mtx);
		if(good_block)
    	{
			pull->prev_hash = digest;
        	/*
        	 * ok to update my bsb tips which is a bootstrap internal state
        	 * note the block is in Cache in case it cannot be stored yet
        	 *
        	 * note the definition of tip changed from "tip in DB" to "tip in DB or Cache",
        	 * to ease the pull generation, we still keep the logical order, i.e. BSB -> MB -> EB
        	 */
        	UpdateMyBSBTip(block);

        	if(digest == pull->target)
        	{
    			LOG_TRACE(log) << "Puller::BSBReceived: one pull request done";
            	ongoing_pulls.erase(pull);

            	auto & working_mbp = working_epoch.two_mbps?
            						 working_epoch.next_mbp : working_epoch.cur_mbp;
				assert(working_mbp.bsb_targets.find(digest) != working_mbp.bsb_targets.end());
				working_mbp.bsb_targets.erase(digest);

				if(working_mbp.bsb_targets.empty())//all BSBs and MB (if have one) in block_cache now
				{
					if(working_mbp.mb != nullptr)
					{
						UpdateMyMBTip(working_mbp.mb);
						if(working_mbp.mb->last_micro_block)
						{
							if(working_epoch.eb != nullptr)
							{
								UpdateMyEBTip(working_epoch.eb);
							}
						}
					}
//							state = PullState::Micro;//Epoch;
//						}
//						else
//						{
//							state = PullState::Micro;
//						}
//					}
//					else // not MB, also means not EB
//					{
//						state = PullState::Micro;//Epoch;
//					}

					//check before go to next micro period
					CheckMicroProgress();
					CreateMorePulls();
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

    void Puller::CreateMorePulls()
    {
		LOG_TRACE(log) << "Puller::"<<__func__;
    	// should be called with mtx locked
    	// should be called only when both waiting_pulls and ongoing_pulls are empty
    	assert(waiting_pulls.empty() && ongoing_pulls.empty());

    	switch (state) {
			case PullState::Epoch:
				working_epoch = {my_tips.eb.epoch+1};
				if(my_tips.eb < others_tips.eb)
				{
					waiting_pulls.push_back(std::make_shared<PullRequest>(
							ConsensusType::Epoch,
							//working_epoch.epoch_num,
							my_tips.eb.digest));
					return;
				}else{
					state = PullState::Micro;
					CreateMorePulls();
				}
				break;

			case PullState::Micro:
				assert(working_epoch.cur_mbp.bsb_targets.empty());

				if(my_tips.mb < others_tips.mb)
				{
					waiting_pulls.push_back(std::make_shared<PullRequest>(
							ConsensusType::MicroBlock,
							//working_epoch.epoch_num,
							my_tips.mb.digest));
					return;
				}else{
					state = PullState::Batch_No_MB;
					CreateMorePulls();
#if 1
					{
		            	auto & working_mbp = working_epoch.two_mbps?
		            						 working_epoch.next_mbp : working_epoch.cur_mbp;
						assert(working_mbp.mb == nullptr);
					}
#endif
				}
				break;

			case PullState::Batch:
			{
				bool added_pulls = false;
				// if pulled a MB, use it to set up pulls
            	auto & working_mbp = working_epoch.two_mbps?
            						 working_epoch.next_mbp : working_epoch.cur_mbp;
            	assert(working_mbp.mb != nullptr);
				assert(my_tips.mb.sqn == working_mbp.mb->sequence -1);
				assert(working_mbp.bsb_targets.empty());

				for(uint i = 0; i < NUM_DELEGATES; ++i)
				{
					//cannot: if(my_tips.bsb_vec[i] < others_tips.bsb_vec[i])
					//TODO add sqn to tip_db and micro_block tips
					//and replace others_tips.bsb_vec with micro_block
					//TODO work around is to update others_tips too and compare
					//my_tips.bsb_vec[i] <= others_tips.bsb_vec[i] && my_tips.bsb_vec[i] != MB.tips[i]
					//TODO for now just check my_tips.bsb_vec[i] != MB.tips[i].
					if(my_tips.bsb_vec[i].digest != working_mbp.mb->tips[i])
					{
						waiting_pulls.push_back(std::make_shared<PullRequest>(
								ConsensusType::BatchStateBlock,
								//working_epoch.epoch_num,
								my_tips.bsb_vec[i].digest,
								working_mbp.mb->tips[i]));
						working_mbp.bsb_targets.insert(working_mbp.mb->tips[i]);
						added_pulls = true;
					}
				}
				if(!added_pulls)
				{
					//no bsb to pull, check before go to next micro period
					CheckMicroProgress();
					CreateMorePulls();
				}
				break;
			}
			case PullState::Batch_No_MB:
			{
				bool added_pulls = false;
				/*
				 * since we don't have any MB, we are at the end of the bootstrap
				 * we consider the two cases that we need to pull BSBs
				 * (1) my BSB tips are behind in the working epoch
				 */
				assert(working_epoch.eb == nullptr);
            	auto & working_mbp = working_epoch.two_mbps?
            						 working_epoch.next_mbp : working_epoch.cur_mbp;
                for(uint i = 0; i < NUM_DELEGATES; ++i)
                {
                    if(my_tips.bsb_vec[i] < others_tips.bsb_vec[i])
                    {
                    	waiting_pulls.push_back(std::make_shared<PullRequest>(
                    			ConsensusType::BatchStateBlock,
								//working_epoch.epoch_num,
								my_tips.bsb_vec[i].digest,
								others_tips.bsb_vec[i].digest));
                    	working_mbp.bsb_targets.insert(others_tips.bsb_vec[i].digest);
                    	added_pulls = true;
                    }
                }
                if(added_pulls)
                {
                	return;
                }
                else
                {
                	//check before go to next epoch
					CheckMicroProgress();
                }
				/*
				 * (2) my BSB tips in bsb_vec_new_epoch is behind.
				 * Note that at this point, the current epoch's last MB and EB are not
				 * (created)received yet. No more to do for the current epoch.
				 * So for (2), we move to the new epoch
				 */
                working_epoch = {working_epoch.epoch_num+1};
                assert(working_epoch.epoch_num == final_ep_number);
				for(uint i = 0; i < NUM_DELEGATES; ++i)
				{
					if(my_tips.bsb_vec_new_epoch[i] < others_tips.bsb_vec_new_epoch[i])
					{
						waiting_pulls.push_back(std::make_shared<PullRequest>(
								ConsensusType::BatchStateBlock,
								//working_epoch.epoch_num,
								my_tips.bsb_vec_new_epoch[i].digest,
								others_tips.bsb_vec_new_epoch[i].digest));
						working_epoch.cur_mbp.bsb_targets.insert(others_tips.bsb_vec_new_epoch[i].digest);
						added_pulls = true;
					}
				}

				if(!added_pulls)
				{
					state = PullState::Done;
				}
				break;
			}
			case PullState::Done:
			default:
				break;
		}
    }

    void Puller::CheckMicroProgress()
    {
		LOG_TRACE(log) << "Puller::"<<__func__;
		assert(working_epoch.cur_mbp.bsb_targets.empty());
		//reduce two mbps to one mbp
		if(working_epoch.two_mbps)
		{
			assert(working_epoch.cur_mbp.mb != nullptr);
			assert(working_epoch.next_mbp.bsb_targets.empty());

			auto digest(working_epoch.cur_mbp.mb->Hash());
			bool mb_processed = !block_cache.IsBlockCached(digest);
			if(mb_processed)
			{
				working_epoch.cur_mbp = working_epoch.next_mbp;
				working_epoch.two_mbps = false;
				working_epoch.next_mbp.Clean();
			}
			else
			{
				LOG_FATAL(log) << "Puller::CreateMorePulls: pulled two MB periods,"
								<< " but first MB has not been processed."
								<< " epoch_num=" << working_epoch.epoch_num
								<< " first MB hash=" << digest.to_string ();
				trace_and_halt();
			}
		}

		if(working_epoch.cur_mbp.mb != nullptr)
		{
			auto digest(working_epoch.cur_mbp.mb->Hash());
			bool mb_processed = !block_cache.IsBlockCached(digest);
			if(mb_processed)
			{
				if(working_epoch.cur_mbp.mb->last_micro_block)
				{
					if(working_epoch.eb != nullptr)
					{
						bool eb_processed = !block_cache.IsBlockCached(working_epoch.eb->Hash());
						if(eb_processed)
						{
							LOG_INFO(log) << "Puller::BSBReceived: processed an epoch "<< working_epoch.epoch_num;
						}
						else
						{
							LOG_FATAL(log) << "Puller::BSBReceived: cannot process epoch block after last micro block "
										   << working_epoch.epoch_num;
							trace_and_halt();
						}
					}
					else
					{
						assert(working_epoch.epoch_num+1 == final_ep_number);
						LOG_INFO(log) << "Puller::BSBReceived: have last MB but not EB "<< working_epoch.epoch_num;
					}
					state = PullState::Epoch;
				}
				else
				{
					state = PullState::Micro;
				}
				working_epoch.cur_mbp.Clean();
			}
			else
			{
				working_epoch.two_mbps = true;
				state = PullState::Micro;
			}
		}
    }

    void Puller::UpdateMyBSBTip(BSBPtr block)
    {
		LOG_TRACE(log) << "Puller::"<<__func__;
		auto d_idx = block->primary_delegate;
    	BlockHash digest = block->Hash();
    	//try old epoch
    	if(my_tips.bsb_vec[d_idx].digest == block->previous)
    	{
        	my_tips.bsb_vec[d_idx].digest = digest;
        	my_tips.bsb_vec[d_idx].epoch = block->epoch_number;
        	my_tips.bsb_vec[d_idx].sqn =  block->sequence;
    	}
    	else if(my_tips.bsb_vec_new_epoch[d_idx].digest == block->previous)
    	{
        	my_tips.bsb_vec_new_epoch[d_idx].digest = digest;
        	my_tips.bsb_vec_new_epoch[d_idx].epoch = block->epoch_number;
        	my_tips.bsb_vec_new_epoch[d_idx].sqn =  block->sequence;
    	}
    	else
    	{
    		assert(false);
    	}
    }

    void Puller::UpdateMyMBTip(MBPtr block)
    {
		LOG_TRACE(log) << "Puller::"<<__func__;
    	assert(my_tips.mb.digest == block->previous);
    	my_tips.mb.digest = block->Hash();
    	my_tips.mb.epoch = block->epoch_number;
    	my_tips.mb.sqn =  block->sequence;
    }

    void Puller::UpdateMyEBTip(EBPtr block)
    {
		LOG_TRACE(log) << "Puller::"<<__func__;
    	assert(my_tips.eb.digest == block->previous);
    	my_tips.eb.digest = block->Hash();
    	my_tips.eb.epoch = block->epoch_number;
    	my_tips.eb.sqn =  block->epoch_number;
    }

    ///////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////

    PullRequestHandler::PullRequestHandler(PullRequest request, Store & store)
    : request(request)
    , store(store)
    {
		LOG_TRACE(log) << "PullRequestHandler::"<<__func__;
		std::vector<uint8_t> buf;
    	uint32_t block_size = GetBlock(request.prev_hash, buf);
		if(block_size > 0)//have block
		{
			memcpy (next.data(), buf.data() + PullResponseReserveSize + block_size - HASH_SIZE, HASH_SIZE);
		}
    }

    uint32_t PullRequestHandler::GetBlock(BlockHash & hash, std::vector<uint8_t> & buf)
    {
		LOG_TRACE(log) << "PullRequestHandler::"<<__func__;
		switch (request.block_type) {
			case ConsensusType::BatchStateBlock:
				return 0;//TODO store.batch_block_get_raw(hash, PullResponseReserveSize, buf);
			case ConsensusType::MicroBlock:
				return 0;//TODO  store.micro_block_get_raw(hash, PullResponseReserveSize, buf);
			case ConsensusType::Epoch:
				return 0;//TODO  store.epoch_get_raw(hash, PullResponseReserveSize, buf);
			default:
				return 0;
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
    		if(next == request.target)
    		{
    			status = PullResponseStatus::LastBlock;
    		}
    		else
    		{
        		memcpy (next.data(), buf.data() + PullResponseReserveSize + block_size - HASH_SIZE, HASH_SIZE);
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
    	PullResponseSerializedLeadingFields(request.block_type, status, block_size, buf);
    	return status == PullResponseStatus::MoreBlock;
    }

}//namespace




//				if(working_epoch.two_mbps)
//				{
//					assert(working_epoch.cur_mbp.mb != nullptr);
//					assert(working_epoch.next_mbp.bsb_targets.empty());
//
//					auto digest(working_epoch.cur_mbp.mb->Hash());
//					bool mb_processed = !block_cache.IsMBCached(working_epoch.epoch_num, digest);
//					if(mb_processed)
//					{
//						working_epoch.cur_mbp = working_epoch.next_mbp;
//						working_epoch.two_mbps = false;
//					}
//					else
//					{
//						LOG_FATAL(log) << "Puller::CreateMorePulls: pulled two MB periods,"
//										<< " but first MB has not been processed."
//										<< " epoch_num=" << working_epoch.epoch_num
//										<< " MB_1 hash=" << digest;
//						trace_and_halt();
//					}
//				}
//
//				if(working_epoch.cur_mbp.mb != nullptr)
//				{
//					auto digest(working_epoch.cur_mbp.mb->Hash());
//					bool mb_processed = !block_cache.IsMBCached(working_epoch.epoch_num, digest);
//					if(mb_processed)
//					{
//
//						if(working_epoch.eb != nullptr)
//						{
//							bool eb_processed = !block_cache.IsEBCached(working_epoch.epoch_num);
//							if(eb_processed)
//							{
//								//working_epoch too?
//								LOG_INFO(log) << "Puller::BSBReceived: processed an epoch "<< working_epoch.epoch_num;
//							}
//							else
//							{
//								LOG_FATAL(log) << "Puller::BSBReceived: cannot process epoch block after last micro block "
//											   << working_epoch.epoch_num;
//								trace_and_halt();
//							}
//						}
//					}
//					else
//					{
//						working_epoch.two_mbps = true;
//
//						//						waiting_pulls.push_back(std::make_shared<PullRequest>(
//						//								ConsensusType::MicroBlock,
//						//								working_epoch.epoch_num,
//						//								my_tips.mb.digest));
//						//						return;
//					}
//				}
//    uint32_t Puller::ComputeNumBSBToPull(Tip &a, Tip &b)
//    {
//    	assert(a.epoch == b.epoch && a < b);
//    	if(a.digest == 0)
//        {
//    		return b.sqn - a.sqn + 1;
//        }
//    	else
//    	{
//    		return b.sqn - a.sqn;
//    	}
//    }
//        						if(working_epoch.eb != nullptr)
//        						{
//        							bool eb_processed = !block_cache.IsEBCached(working_epoch.epoch_num);
//        							if(eb_processed)
//        							{
//        								//TODO update eb tip
//        								//working_epoch too?
//        								LOG_INFO(log) << "Puller::BSBReceived: processed an epoch "<< working_epoch.epoch_num;
//        							}
//        							else
//        							{
//        								LOG_FATAL(log) << "Puller::BSBReceived: cannot process epoch block after last micro block "
//        										   	   << working_epoch.epoch_num;
//        								trace_and_halt();
//        							}
//        						}
//        						{
//        							if(working_epoch.epoch_num == others_tips.);
//        						}
//        					}
//
//
//        					//////////////////////
//
//            				bool mb_processed = !block_cache.IsMBCached(working_epoch.epoch_num, digest);
//            				if(mb_processed)
//            				{
//            					if(!working_mbp.mb->last_micro_block)
//            					{
//            						state = PullState::Micro;
//
//                        			return PullStatus::Done;
//            					}
//            					else
//            					{
//            						state = PullState::Epoch;
//            						if(working_epoch.eb != nullptr)
//            						{
//            							bool eb_processed = !block_cache.IsEBCached(working_epoch.epoch_num);
//            							if(eb_processed)
//            							{
//            								//TODO update eb tip
//            								//working_epoch too?
//            								LOG_INFO(log) << "Puller::BSBReceived: processed an epoch "<< working_epoch.epoch_num;
//            							}
//            							else
//            							{
//            								LOG_FATAL(log) << "Puller::BSBReceived: cannot process epoch block after last micro block "
//            										   	   << working_epoch.epoch_num;
//            								trace_and_halt();
//            							}
//            						}
//            						{
//            							if(working_epoch.epoch_num == others_tips.);
//            						}
//            					}
//
//            				}
//            				else
//            				{
//            					working_epoch.two_mbps = true;
//            				}
//            			}
//            			else //no mb, (1) we are done, or (2) last MB in epoch not received, but newest epoch started
//            			{
//            				state = PullState::Epoch;
//            			}
