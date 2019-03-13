#include <logos/consensus/persistence/block_cache.hpp>

//
//
//bool BlockCache::AddEB(EBPtr block)
//{
//	if(false)//!eb_handler.VerifyAggSignature(*block))
//	{
//		LOG_TRACE(log) << "BlockCache::AddEB: VerifyAggSignature failed";
//		return false;
//	}
//
//	//safe to ignore the block for both p2p and bootstrap
//	if(eb_handler.BlockExists(*block))
//	{
//		LOG_TRACE(log) << "BlockCache::AddEB: BlockExists";
//		return true;
//	}
//
//	bool found = false;
//	for(auto bi = epochs.rbegin(); bi != epochs.rend(); ++ bi)
//	{
//		if(bi->epoch_num == block->epoch_number)
//		{
//			//duplicate
//			found = true;
//			break;
//		}
//		else if(bi->epoch_num < block->epoch_number)
//		{
//			epochs.emplace(bi.base(), Epoch(block));
//			found = true;
//			break;
//		}
//	}
//	if(!found)
//	{
//		epochs.emplace_front(Epoch(block));
//		Validate();//TODO optimize: Validate eb first
//	}
//
//	return true;
//}
//
//bool BlockCache::AddMB(MBPtr block)
//{
//	if(false)//!mb_handler.VerifyAggSignature(*block))
//	{
//		LOG_TRACE(log) << "BlockCache::AddMB: VerifyAggSignature failed";
//		return false;
//	}
//	//safe to ignore the block for both p2p and bootstrap
//	if(mb_handler.BlockExists(*block))
//	{
//		LOG_TRACE(log) << "BlockCache::AddMB: BlockExists";
//		return true;
//	}
//
//	bool found = false;
//	bool add2begin = false;
//	for(auto bi = epochs.rbegin(); bi != epochs.rend(); ++ bi)
//	{
//		if(bi->epoch_num == block->epoch_number)
//		{
//			for(auto mbi = bi->mbs.rbegin(); mbi != bi->mbs.rend(); ++ mbi)
//			{
//				if((*mbi)->sequence == block->sequence)
//				{
//					//duplicate
//					found = true;
//					break;
//				}
//				else if ((*mbi)->sequence < block->sequence)
//				{
//					bi->mbs.emplace(mbi.base(), block);
//					found = true;
//					break;
//				}
//			}
//			if(!found)
//			{
//				bi->mbs.emplace_front(block);
//				found = true;
//				auto temp_i = bi;
//				add2begin = ++temp_i == epochs.rend();
//			}
//			break;
//		}
//		else if(bi->epoch_num < block->epoch_number)
//		{
//			epochs.emplace(bi.base(), Epoch(block));
//			found = true;
//			break;
//		}
//	}
//	if(!found)
//	{
//		epochs.emplace_front(Epoch(block));
//		found = true;
//		add2begin = true;
//	}
//	if(add2begin)
//	{
//		Validate();//TODO optimize: Validate mb first
//	}
//
//	return true;
//}
//
//bool BlockCache::AddBSB(BSBPtr block)
//{
//	if(false)//!bsb_handler.VerifyAggSignature(*block))
//	{
//		LOG_TRACE(log) << "BlockCache::AddBSB: VerifyAggSignature failed";
//		return false;
//	}
//
//	//safe to ignore the block for both p2p and bootstrap
//	if(bsb_handler.BlockExists(*block))
//	{
//		LOG_TRACE(log) << "BlockCache::AddMB: BlockExists";
//		return true;
//	}
//
//	bool found = false;
//	bool add2begin = false;
//	for(auto bi = epochs.rbegin(); bi != epochs.rend(); ++ bi)
//	{
//		if(bi->epoch_num == block->epoch_number)
//		{
//			for(auto bsbi = bi->bsbs[block->primary_delegate].rbegin();
//					bsbi != bi->bsbs[block->primary_delegate].rend(); ++ bsbi)
//			{
//				if((*bsbi)->sequence == block->sequence)
//				{
//					//duplicate
//					found = true;
//					break;
//				}
//				else if ((*bsbi)->sequence < block->sequence)
//				{
//					bi->bsbs[block->primary_delegate].emplace(bsbi.base(), block);
//					found = true;
//					break;
//				}
//			}
//			if(!found)
//			{
//				bi->bsbs[block->primary_delegate].emplace_front(block);
//				found = true;
//				auto temp_i = bi;
//				add2begin = ++temp_i == epochs.rend();
//			}
//			break;
//		}
//		else if(bi->epoch_num < block->epoch_number)
//		{
//			epochs.emplace(bi.base(), Epoch(block));
//			found = true;
//			break;
//		}
//	}
//	if(!found)
//	{
//		epochs.emplace_front(Epoch(block));
//		found = true;
//		add2begin = true;
//	}
//	if(add2begin)
//	{
//		Validate(block->primary_delegate);
//	}
//
//	return true;
//}
//
//bool BlockCache::IsBSBCached(uint32_t epoch_num, BlockHash block_hash);
//bool BlockCache::IsMBCached(uint32_t epoch_num, BlockHash block_hash);
//bool BlockCache::IsEBCached(uint32_t epoch_num);
//
//
//void BlockCache::Validate(uint8_t bsb_idx = 0)
//{
//	assert(bsb_idx<=NUM_DELEGATES);
//	while(!epochs.empty())
//	{
//		uint num_bsb_chain_no_progress = 0;
//		auto e = epochs.begin();
//
//		// try bsb chains till num_bsb_chain_no_progress reaches NUM_DELEGATES
//		while(num_bsb_chain_no_progress < NUM_DELEGATES)
//		{
//			std::list<BSBPtr>::iterator to_validate = e->bsbs[bsb_idx].begin();
//			if(to_validate == e->bsbs[bsb_idx].end())
//			{
//				//cannot make progress with empty list
//				num_bsb_chain_no_progress++;
//				bsb_idx = (bsb_idx+1)%NUM_DELEGATES;
//			}
//			else
//			{
//				ApprovedBSB & block = *(*to_validate);
//				ValidationStatus status;
//				if(bsb_handler.Validate(block, &status))
//				{
//					bsb_handler.ApplyUpdates(block, block.primary_delegate);
//					e->bsbs[bsb_idx].pop_front();
//					num_bsb_chain_no_progress = 0;
//				}
//				else
//				{
//					switch (status.reason) {
//						case logos::process_result::gap_previous:
//						case logos::process_result::gap_source:
//							num_bsb_chain_no_progress++;
//							bsb_idx = (bsb_idx+1)%NUM_DELEGATES;
//							break;
//						default:
//							//Since the agg-sigs are already verified, we expect gap etc reasons.
//							//For any other reason, we log them, and investigate.
//							//(TODO) detect double spend?
//							LOG_INFO(log) << "BlockCache::Validate BSB status: "
//									<< ProcessResultToString(status.reason)
//									<< " block hash " << block.Hash();
//							//Throw the block out, otherwise it blocks the rest.
//							e->bsbs[bsb_idx].pop_front();
//							break;
//					}
//				}
//			}
//		}
//
//		bool mbs_empty = e->mbs.empty();
//		bool last_mb = false;
//		while(!e->mbs.empty())
//		{
//			ApprovedMB & block = *(e->mbs.front());
//			ValidationStatus status;
//			if(mb_handler.Validate(block, &status))
//			{
//				mb_handler.ApplyUpdates(block, block.primary_delegate);
//				last_mb = block.last_micro_block;
//				e->mbs.pop_front();
//				if(last_mb) assert(e->mbs.empty());
//			}
//			else
//			{
//				LOG_INFO(log) << "BlockCache::Validate MB status: "
//							<< ProcessResultToString(status.reason)
//							<< " block hash " << block.Hash();
//			}
//		}
//
//		bool eb_stored = false;
//		if((last_mb || mbs_empty) && e->eb != nullptr)
//		{
//			ApprovedEB & block = *e->eb;
//			ValidationStatus status;
//			if(eb_handler.Validate(block, &status))
//			{
//				eb_handler.ApplyUpdates(block, block.primary_delegate);
//				LOG_INFO(log) << "BlockCache::Validated EB, block hash: " << block.Hash();
//				epochs.pop_front();
//				eb_stored = true;
//			}
//		}
//
//		bool last_epoch_begin = epochs.size()==1 && epochs.front().mbs.empty();
//		if( (!eb_stored) && (!last_epoch_begin))
//			break;
//	}
//}

