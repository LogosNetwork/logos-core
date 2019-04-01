#include <logos/consensus/persistence/block_cache.hpp>

BlockCache::BlockCache(Store &store)
	: eb_handler(store)
  	, mb_handler(store)
	, bsb_handler(store)
	{}

bool BlockCache::AddEB(EBPtr block)
{
	LOG_TRACE(log) << "BlockCache::" << __func__ <<":" << block->CreateTip().to_string();
	if(!eb_handler.VerifyAggSignature(*block))
	{
		LOG_TRACE(log) << "BlockCache::AddEB: VerifyAggSignature failed";
		return false;
	}

	std::lock_guard<std::mutex> lck (mtx);
	//safe to ignore the block for both p2p and bootstrap
	if(eb_handler.BlockExists(*block))
	{
		LOG_TRACE(log) << "BlockCache::AddEB: BlockExists";
		return true;
	}

	bool found = false;
	for(auto bi = epochs.rbegin(); bi != epochs.rend(); ++ bi)
	{
		if(bi->epoch_num == block->epoch_number)
		{
			//duplicate
			if(bi->eb == nullptr)
			{
				bi->eb = block;
	    		cached_blocks.insert(block->Hash());
			}
			found = true;
			break;
		}
		else if(bi->epoch_num < block->epoch_number)
		{
			epochs.emplace(bi.base(), Epoch(block));
    		cached_blocks.insert(block->Hash());
			found = true;
			break;
		}
	}
	if(!found)
	{
		epochs.emplace_front(Epoch(block));
		cached_blocks.insert(block->Hash());
		Validate();//TODO optimize: Validate eb first
	}

	LOG_TRACE(log) << "BlockCache::"<<__func__<<": cached hashes: " << cached_blocks.size();
	for(auto & h : cached_blocks)
		LOG_TRACE(log) << h.to_string();

	return true;
}

bool BlockCache::AddMB(MBPtr block)
{
	LOG_TRACE(log) << "BlockCache::" << __func__ <<":" << block->CreateTip().to_string();
	if(!mb_handler.VerifyAggSignature(*block))
	{
		LOG_TRACE(log) << "BlockCache::AddMB: VerifyAggSignature failed";
		return false;
	}

	std::lock_guard<std::mutex> lck (mtx);
	//safe to ignore the block for both p2p and bootstrap
	if(mb_handler.BlockExists(*block))
	{
		LOG_TRACE(log) << "BlockCache::AddMB: BlockExists";
		return true;
	}

	bool found = false;
	bool add2begin = false;
	for(auto bi = epochs.rbegin(); bi != epochs.rend(); ++ bi)
	{
		if(bi->epoch_num == block->epoch_number)
		{
			for(auto mbi = bi->mbs.rbegin(); mbi != bi->mbs.rend(); ++ mbi)
			{
				if((*mbi)->sequence == block->sequence)
				{
					//duplicate
					found = true;
					break;
				}
				else if ((*mbi)->sequence < block->sequence)
				{
					bi->mbs.emplace(mbi.base(), block);
		    		cached_blocks.insert(block->Hash());

					found = true;
					break;
				}
			}
			if(!found)
			{
				bi->mbs.emplace_front(block);
	    		cached_blocks.insert(block->Hash());

				found = true;
				auto temp_i = bi;
				add2begin = ++temp_i == epochs.rend();
			}
			break;
		}
		else if(bi->epoch_num < block->epoch_number)
		{
			epochs.emplace(bi.base(), Epoch(block));
			add2begin = cached_blocks.empty();
    		cached_blocks.insert(block->Hash());
			found = true;
			break;
		}
	}
	if(!found)
	{
		epochs.emplace_front(Epoch(block));
		cached_blocks.insert(block->Hash());
		found = true;
		add2begin = true;
	}
	if(add2begin)
	{
		Validate();//TODO optimize: Validate mb first
	}

	LOG_TRACE(log) << "BlockCache::"<<__func__<<": cached hashes: " << cached_blocks.size();
	for(auto & h : cached_blocks)
		LOG_TRACE(log) << h.to_string();

	return true;
}

bool BlockCache::AddBSB(BSBPtr block)
{
	LOG_TRACE(log) << "BlockCache::" << __func__ <<":" << block->CreateTip().to_string();

	if(!bsb_handler.VerifyAggSignature(*block))
	{
		LOG_TRACE(log) << "BlockCache::AddBSB: VerifyAggSignature failed";
		return false;
	}

	std::lock_guard<std::mutex> lck (mtx);
	//safe to ignore the block for both p2p and bootstrap
	if(bsb_handler.BlockExists(*block))
	{
		LOG_TRACE(log) << "BlockCache::AddMB: BlockExists";
		return true;
	}

	bool found = false;
	bool add2begin = false;
	for(auto bi = epochs.rbegin(); bi != epochs.rend(); ++ bi)
	{
		if(bi->epoch_num == block->epoch_number)
		{
			for(auto bsbi = bi->bsbs[block->primary_delegate].rbegin();
					bsbi != bi->bsbs[block->primary_delegate].rend(); ++ bsbi)
			{
				if((*bsbi)->sequence == block->sequence)
				{
					//duplicate
					found = true;
					break;
				}
				else if ((*bsbi)->sequence < block->sequence)
				{
					bi->bsbs[block->primary_delegate].emplace(bsbi.base(), block);
		    		cached_blocks.insert(block->Hash());

					found = true;
					break;
				}
			}
			if(!found)
			{
				bi->bsbs[block->primary_delegate].emplace_front(block);
	    		cached_blocks.insert(block->Hash());

				found = true;
				add2begin = true;
//				auto temp_i = bi;
//				add2begin = ++temp_i == epochs.rend();
			}
			break;
		}
		else if(bi->epoch_num < block->epoch_number)
		{
			epochs.emplace(bi.base(), Epoch(block));
    		cached_blocks.insert(block->Hash());
			found = true;
			add2begin = true;
			break;
		}
	}
	if(!found)
	{
		epochs.emplace_front(Epoch(block));
		cached_blocks.insert(block->Hash());
		found = true;
		add2begin = true;
	}
	if(add2begin)
	{
		Validate(block->primary_delegate);
	}

	LOG_TRACE(log) << "BlockCache::"<<__func__<<": cached hashes: " << cached_blocks.size();
	for(auto & h : cached_blocks)
		LOG_TRACE(log) << h.to_string();

	return true;
}

bool BlockCache::IsBlockCached(const BlockHash & b)
{
	LOG_TRACE(log) << "BlockCache::" << __func__ << ":" << b.to_string();
	std::lock_guard<std::mutex> lck (mtx);
	return cached_blocks.find(b) != cached_blocks.end();
}

void BlockCache::Validate(uint8_t bsb_idx)
{
	LOG_TRACE(log) << "BlockCache::"<<__func__<<"{";
	assert(bsb_idx<=NUM_DELEGATES);
	auto e = epochs.begin();
	while( e != epochs.end())
	{
		uint num_bsb_chain_no_progress = 0;
		// try bsb chains till num_bsb_chain_no_progress reaches NUM_DELEGATES
		while(num_bsb_chain_no_progress < NUM_DELEGATES)
		{
			LOG_TRACE(log) << "BlockCache::Validate num_bsb_chain_no_progress="
					<< num_bsb_chain_no_progress;
			for(;;)
			{
				std::list<BSBPtr>::iterator to_validate = e->bsbs[bsb_idx].begin();
				if(to_validate == e->bsbs[bsb_idx].end())
				{
					//cannot make progress with empty list
					num_bsb_chain_no_progress++;
					bsb_idx = (bsb_idx+1)%NUM_DELEGATES;
					break;//for(;;)
				}
				else
				{
					ApprovedRB & block = *(*to_validate);
					ValidationStatus status;
					LOG_TRACE(log) << "BlockCache::"<<__func__<<": verifying "
							<< block.CreateTip().to_string();

					if(bsb_handler.VerifyContent(block, &status))
					{
						bsb_handler.ApplyUpdates(block, block.primary_delegate);
						cached_blocks.erase(block.Hash());
						e->bsbs[bsb_idx].pop_front();
						num_bsb_chain_no_progress = 0;
					}
					else
					{
						LOG_TRACE(log) << "BlockCache::Validate BSB status: "
								<< ProcessResultToString(status.reason);
	#ifdef REASON_CLEARED
						switch (status.reason) {
							case logos::process_result::gap_previous:
							case logos::process_result::gap_source:
								//TODO any other cases that can be considered as gap?
								num_bsb_chain_no_progress++;
								bsb_idx = (bsb_idx+1)%NUM_DELEGATES;
								break;
							default:
								//Since the agg-sigs are already verified,
								//we expect gap-like reasons.
								//For any other reason, we log them, and investigate.
								LOG_ERROR(log) << "BlockCache::Validate BSB status: "
										<< ProcessResultToString(status.reason)
										<< " block " << block.CreateTip().to_string();
								//Throw the block out, otherwise it blocks the rest.
								cached_blocks.erase(block.Hash());
								e->bsbs[bsb_idx].pop_front();
								//TODO recall?
								//TODO detect double spend?
								break;
						}
	#else
						num_bsb_chain_no_progress++;
						bsb_idx = (bsb_idx+1)%NUM_DELEGATES;
	#endif
						break;//for(;;)
					}
				}
			}

//
//
//
//
//			std::list<BSBPtr>::iterator to_validate = e->bsbs[bsb_idx].begin();
//			if(to_validate == e->bsbs[bsb_idx].end())
//			{
//				//cannot make progress with empty list
//				num_bsb_chain_no_progress++;
//				bsb_idx = (bsb_idx+1)%NUM_DELEGATES;
//			}
//			else
//			{
//				ApprovedRB & block = *(*to_validate);
//				ValidationStatus status;
//				if(bsb_handler.VerifyContent(block, &status))
//				{
//					bsb_handler.ApplyUpdates(block, block.primary_delegate);
//					cached_blocks.erase(block.Hash());
//					e->bsbs[bsb_idx].pop_front();
//					num_bsb_chain_no_progress = 0;
//				}
//				else
//				{
//#ifdef REASON_CLEARED
//					switch (status.reason) {
//						case logos::process_result::gap_previous:
//						case logos::process_result::gap_source:
//							//TODO any other cases that can be considered as gap?
//							num_bsb_chain_no_progress++;
//							bsb_idx = (bsb_idx+1)%NUM_DELEGATES;
//							break;
//						default:
//							//Since the agg-sigs are already verified,
//							//we expect gap-like reasons.
//							//For any other reason, we log them, and investigate.
//							LOG_ERROR(log) << "BlockCache::Validate BSB status: "
//									<< ProcessResultToString(status.reason)
//									<< " block " << block.CreateTip().to_string();
//							//Throw the block out, otherwise it blocks the rest.
//							cached_blocks.erase(block.Hash());
//							e->bsbs[bsb_idx].pop_front();
//							//TODO recall?
//							//TODO detect double spend?
//							break;
//					}
//#else
//					num_bsb_chain_no_progress++;
//					bsb_idx = (bsb_idx+1)%NUM_DELEGATES;
//#endif
//
//				}
//			}
		}

		bool mbs_empty = e->mbs.empty();
		bool last_mb = false;
		while(!e->mbs.empty())
		{
			ApprovedMB & block = *(e->mbs.front());
			ValidationStatus status;
			if(mb_handler.VerifyContent(block, &status))
			{
				mb_handler.ApplyUpdates(block, block.primary_delegate);
				last_mb = block.last_micro_block;
				cached_blocks.erase(block.Hash());
				e->mbs.pop_front();
				if(last_mb)
					assert(e->mbs.empty());
			}
			else
			{
				LOG_TRACE(log) << "BlockCache::Validate MB status: "
						<< ProcessResultToString(status.reason);
#ifdef REASON_CLEARED
				switch (status.reason) {
					case logos::process_result::gap_previous:
					case logos::process_result::gap_source:
						//TODO any other cases that can be considered as gap?
						break;
					default:
						LOG_ERROR(log) << "BlockCache::Validate MB status: "
							<< ProcessResultToString(status.reason)
							<< " block " << block.CreateTip().to_string();
						cached_blocks.erase(block.Hash());
						e->mbs.pop_front();
						//TODO recall?
						break;
				}
#endif
				break;
			}
		}

		bool e_finished = false;
		if(last_mb || mbs_empty)
		{
			if( e->eb != nullptr)
			{
				ApprovedEB & block = *e->eb;
				ValidationStatus status;
				if(eb_handler.VerifyContent(block, &status))
				{
					eb_handler.ApplyUpdates(block, block.primary_delegate);
					LOG_INFO(log) << "BlockCache::Validated EB, block: "
								  << block.CreateTip().to_string();
					cached_blocks.erase(block.Hash());
					epochs.erase(e);
					e_finished = true;
				}
				else
				{
					LOG_TRACE(log) << "BlockCache::Validate EB status: "
							<< ProcessResultToString(status.reason);
	#ifdef REASON_CLEARED
					switch (status.reason) {
						case logos::process_result::gap_previous:
						case logos::process_result::gap_source:
							//TODO any other cases that can be considered as gap?
							break;
						default:
							LOG_ERROR(log) << "BlockCache::Validate EB status: "
								<< ProcessResultToString(status.reason)
								<< " block " << block.CreateTip().to_string();
							cached_blocks.erase(block.Hash());
							epochs.pop_front();
							//TODO recall?
							break;
					}
	#endif
				}
			}
			else
			{
				LOG_INFO(log) << "BlockCache::Validated, no MB, no EB, e#=" << e->epoch_num;
			}
		}

		if(e_finished)
		{
			e = epochs.begin();
		}
		else
		{
			//two-tip case, i.e. first 10 minutes of the latest epoch
			bool last_epoch_begin = epochs.size()==2 &&
					e->eb == nullptr &&
					e->mbs.empty() &&
					(++e)->mbs.empty();
			if(!last_epoch_begin)
			{
				e = epochs.end();
			}
			else
			{
				//already did ++e
			}
		}
	}
	LOG_ERROR(log) << "BlockCache::"<<__func__<<"}";
}

