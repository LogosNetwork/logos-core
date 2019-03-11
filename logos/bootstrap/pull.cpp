#include <logos/node/common.hpp>
#include <logos/node/node.hpp>

#include <logos/bootstrap/pull.hpp>
#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/tips.hpp>

namespace Bootstrap
{
	Puller::Puller(BlockCache & block_cache, Store & store)
	: block_cache(block_cache)
	, store(store)
	, state(PullState::Epoch)
	{}

	void Puller::Init(TipSet &my_tips, TipSet &others_tips)
	{
		std::lock_guard<std::mutex> lck (mtx);
		this->my_tips = my_tips;
		this->others_tips = others_tips;
		if(my_tips.IsBehind(others_tips))
		{
			state = PullState::Epoch;
			working_epoch = {my_tips.eb.epoch+1, nullptr, nullptr, nullptr};
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
		auto insert_res = ongoing_pulls.insert({pull, pull->num_blocks});
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
    	assert(state==PullState::Epoch && working_epoch.eb == nullptr);
    	if(block->previous != pull->prev_hash)
    	{
    		return PullStatus::DisconnectSender;
    	}
    	//TODO verify agg-signatures
    	working_epoch.eb = block;
    	state = PullState::Micro;
    	ongoing_pulls.erase(pull);
    	//TODO remove pull from on-going
    	CreateMorePulls();
    	return PullStatus::Done;
    }

    PullStatus Puller::MBReceived(PullPtr pull, MBPtr block)
    {
    	assert(state==PullState::Micro);
    	if(block->previous != pull->prev_hash)
    	{
    		return PullStatus::DisconnectSender;
    	}
    	//TODO verify agg-signatures
    	state = PullState::Batch;
    	ongoing_pulls.erase(pull);
    	if(working_epoch.need_next_mb)
    	{
    		assert(working_epoch.next_mb.mb == nullptr);
    		working_epoch.next_mb.mb == block;
    		ExtraMicroBlock();
    	}
    	else
    	{
    		assert(working_epoch.cur_mb.mb == nullptr);
    		working_epoch.cur_mb.mb == block;
        	CreateMorePulls();
    	}

     	return PullStatus::Done;

    }
	PullStatus Puller::BSBReceived(PullPtr pull, BSBPtr block, bool last_block)
	{
    	assert(state==PullState::Batch);
    	if(block->previous != pull->prev_hash)
    	{
    		return PullStatus::DisconnectSender;
    	}
    	//TODO verify agg-signatures
    	if(block->Hash() == pull->target)
    	{
        	ongoing_pulls.erase(pull);
    	}

	}


    void Puller::PullFailed(PullPtr pull)
    {
    	std::lock_guard<std::mutex> lck (mtx);
    	ongoing_pulls.erase(pull);
    	waiting_pulls.push_front(pull);
    }

    //TODO setup expected tips set
    void Puller::CreateMorePulls()
    {
    	//TODO std::lock_guard<std::mutex> lck (mtx);
    	//TODO assert(waiting_pulls.empty() && ongoing_pulls.empty());
    	switch (state) {
			case PullState::Epoch:
				if(my_tips.eb < others_tips.eb)
				{
					waiting_pulls.push_back(std::make_shared<PullRequest>(
							ConsensusType::Epoch,
							working_epoch.epoch_num,
							my_tips.eb.digest,
							1));
					return;
				}else{
					state = PullState::Micro;
					CreateMorePulls();
				}
				break;
			case PullState::Micro:
				if(my_tips.mb < others_tips.mb)
				{
					waiting_pulls.push_back(std::make_shared<PullRequest>(
							ConsensusType::MicroBlock,
							working_epoch.epoch_num,
							my_tips.mb.digest,
							1));
					return;
				}else{
					state = PullState::Batch;
					CreateMorePulls();
				}
				break;
			case PullState::Batch:
				bool added_pulls = false;
				/*
				 * if pulled a MB, use it to set up pulls
				 */
				if(working_epoch.cur_mb != nullptr)
				{
					assert(my_tips.mb.sqn == working_epoch.cur_mb.mb->sequence -1);

	                for(uint i = 0; i < NUM_DELEGATES; ++i)
	                {
	                    //TODO add sqn to tips in MB?
	                	//if(my_tips.bsb_vec[i] < others_tips.bsb_vec[i])
	                    {
	                    	waiting_pulls.push_back(std::make_shared<PullRequest>(
	                    			ConsensusType::BatchStateBlock,
									working_epoch.epoch_num,
									my_tips.bsb_vec[i].digest,
									working_epoch.cur_mb.mb->tips[i]));
//									ComputeNumBSBToPull(my_tips.bsb_vec[i], others_tips.bsb_vec[i])));
	                    	added_pulls = true;
	                    }
	                }
				}
                if(added_pulls)
                {
                	return;
                }

				/*
				 * since we don't have any MB, we are at the end of the bootstrap
				 * we consider the two cases that we need to pull BSBs
				 * (1) my BSB tips are behind in the working epoch
				 */
				assert(working_epoch.eb == nullptr);
                for(uint i = 0; i < NUM_DELEGATES; ++i)
                {
                    if(my_tips.bsb_vec[i] < others_tips.bsb_vec[i])
                    {
                    	waiting_pulls.push_back(std::make_shared<PullRequest>(
                    			ConsensusType::BatchStateBlock,
								working_epoch.epoch_num,
								my_tips.bsb_vec[i].digest,
								others_tips.bsb_vec[i].digest));
//								ComputeNumBSBToPull(my_tips.bsb_vec[i], others_tips.bsb_vec[i])));
                    	added_pulls = true;
                    }
                }
                if(added_pulls)
                {
                	return;
                }

				/*
				 * (2) my BSB tips in bsb_vec_new_epoch is behind.
				 * Note that at this point, the current epoch's last MB and EB are not
				 * (created)received yet. No more to do for the current epoch.
				 * So for (2), we move to the new epoch
				 */
				for(uint i = 0; i < NUM_DELEGATES; ++i)
				{
					if(my_tips.bsb_vec_new_epoch[i] < others_tips.bsb_vec_new_epoch[i])
					{
						waiting_pulls.push_back(std::make_shared<PullRequest>(
								ConsensusType::BatchStateBlock,
								working_epoch.epoch_num,
								my_tips.bsb_vec_new_epoch[i].digest,
//								ComputeNumBSBToPull(my_tips.bsb_vec_new_epoch[i],
													others_tips.bsb_vec_new_epoch[i].digest));
						added_pulls = true;
					}
				}

				if(added_pulls)
				{
					working_epoch = {working_epoch.epoch_num+1, nullptr, nullptr, nullptr};
					//note we keep state = PullState::Batch;
					return;
				}
				else
				{
					state = PullState::Done;
				}
				break;
			case PullState::Done:
			default:
				break;
		}
    }

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
}

