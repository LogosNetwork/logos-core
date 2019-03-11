#pragma once

#include <memory>
#include <mutex>
#include <unordered_set>

#include <logos/lib/numbers.hpp>
#include <logos/lib/blocks.hpp>
#include <logos/lib/trace.hpp>

#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/consensus/messages/common.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/consensus_manager.hpp>
#include <logos/consensus/persistence/persistence.hpp>
#include <logos/consensus/persistence/persistence_manager.hpp>
#include <logos/consensus/persistence/epoch/nondel_epoch_persistence.hpp>
#include <logos/consensus/persistence/microblock/nondel_microblock_persistence.hpp>
#include <logos/consensus/persistence/microblock/microblock_persistence.hpp>
#include <logos/consensus/persistence/epoch/epoch_persistence.hpp>

#include <logos/epoch/epoch.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
#include <logos/epoch/epoch_handler.hpp>
#include <logos/epoch/recall_handler.hpp>

#include <logos/microblock/microblock.hpp>
#include <logos/microblock/microblock_handler.hpp>

#include <logos/node/node.hpp>

#include <logos/bootstrap/attempt.hpp>
#include <logos/bootstrap/microblock.hpp>
#include <logos/bootstrap/pull_connection.hpp>


class BlockCache
{
public:
    using BSBPtr = std::shared_ptr<ApprovedBSB>;
    using MBPtr = std::shared_ptr<ApprovedMB>;
    using EBPtr = std::shared_ptr<ApprovedEB>;

    bool AddEB(EBPtr block)
    {
    	if(!eb_handler.VerifyAggSignature(*block))
    	{
    		LOG_TRACE(log) << "BlockCache::AddEB: VerifyAggSignature failed";
    		return false;
    	}

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
    			found = true;
    			break;
    		}
    		else if(bi->epoch_num < block->epoch_number)
    		{
    			epochs.emplace(bi.base(), Epoch(block));
    			found = true;
    			break;
    		}
    	}
    	if(!found)
    	{
    		epochs.emplace_front(Epoch(block));
			Validate();//TODO Validate eb first
    	}

    	return true;
    }

    bool AddMB(MBPtr block)
    {
    	if(!mb_handler.VerifyAggSignature(*block))
    	{
    		LOG_TRACE(log) << "BlockCache::AddMB: VerifyAggSignature failed";
    		return false;
    	}
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
    	    			found = true;
    	    			break;
    				}
    			}
    			if(!found)
    			{
    				bi->mbs.emplace_front(block);
        			found = true;
        			auto temp_i = bi;
        			add2begin = ++temp_i == epochs.rend();
    			}
    			break;
    		}
    		else if(bi->epoch_num < block->epoch_number)
    		{
    			epochs.emplace(bi.base(), Epoch(block));
    			found = true;
    			break;
    		}
    	}
		if(!found)
		{
			epochs.emplace_front(Epoch(block));
			found = true;
			add2begin = true;
		}
		if(add2begin)
		{
			Validate();//TODO Validate mb first
		}

    	return true;
    }

    bool AddBSB(BSBPtr block)
    {
    	if(!bsb_handler.VerifyAggSignature(*block))
    	{
    		LOG_TRACE(log) << "BlockCache::AddBSB: VerifyAggSignature failed";
    		return false;
    	}

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
    	    			found = true;
    	    			break;
    				}
    			}
    			if(!found)
    			{
    				bi->bsbs[block->primary_delegate].emplace_front(block);
        			found = true;
        			auto temp_i = bi;
        			add2begin = ++temp_i == epochs.rend();
    			}
    			break;
    		}
    		else if(bi->epoch_num < block->epoch_number)
    		{
    			epochs.emplace(bi.base(), Epoch(block));
    			found = true;
    			break;
    		}
    	}
		if(!found)
		{
			epochs.emplace_front(Epoch(block));
			found = true;
			add2begin = true;
		}
		if(add2begin)
		{
			Validate(block->primary_delegate);
		}

    	return true;
    }

    bool IsMBCached(uint32_t epoch_num, BlockHash block_hash);
    bool IsEBCached(uint32_t epoch_num);
    //TODO need to report bsbs are processed? if so, how

private:
    struct Epoch
	{
    	Epoch(uint32_t epoch_num)
    	: epoch_num(epoch_num)
    	, eb(nullptr)
    	{}
    	Epoch(EBPtr block)
    	: epoch_num(block->epoch_number)
    	, eb(block)
    	{}
    	Epoch(MBPtr block)
    	: epoch_num(block->epoch_number)
    	, eb(nullptr)
    	{
    		mbs.push_front(block);
    	}
    	Epoch(BSBPtr block)
    	: epoch_num(block->epoch_number)
    	, eb(nullptr)
    	{
    		assert(block->primary_delegate < NUM_DELEGATES);
    		bsbs[block->primary_delegate].push_front(block);
    	}

    	uint32_t epoch_num;
    	EBPtr eb;
    	std::list<MBPtr> mbs;
    	std::list<BSBPtr> bsbs[NUM_DELEGATES];
    	//1 for each unprocessed tip of the oldest mb
        //TODO std::bitset<NUM_DELEGATES> mb_dependences;
	};
    std::list<Epoch> epochs;

    /*
     * should be called when a new block is added to the beginning of any chain of the oldest epoch
     */
    void Validate(uint8_t bsb_idx = 0)
    {
    	assert(bsb_idx<=NUM_DELEGATES);
    	while(!epochs.empty())
    	{
			uint num_bsb_chain_no_progress = 0;
			auto e = epochs.begin();

			// try bsb chains till num_bsb_chain_no_progress reaches NUM_DELEGATES
			while(num_bsb_chain_no_progress < NUM_DELEGATES)
			{
				std::list<BSBPtr>::iterator to_validate = e->bsbs[bsb_idx].begin();
				if(to_validate == e->bsbs[bsb_idx].end())
				{
					//cannot make progress with empty list
					num_bsb_chain_no_progress++;
					bsb_idx = (bsb_idx+1)%NUM_DELEGATES;
				}
				else
				{
					ApprovedBSB & block = *(*to_validate);
					ValidationStatus status;
					if(bsb_handler.Validate(block, &status))
					{
						bsb_handler.ApplyUpdates(block, block.primary_delegate);
						e->bsbs[bsb_idx].pop_front();
						num_bsb_chain_no_progress = 0;
					}
					else
					{
						switch (status.reason) {
							case logos::process_result::gap_previous:
							case logos::process_result::gap_source:
								num_bsb_chain_no_progress++;
								bsb_idx = (bsb_idx+1)%NUM_DELEGATES;
								break;
							default:
								//Since the agg-sigs are already verified, we expect gap etc reasons.
								//For any other reason, we log them, and investigate.
								//(TODO) detect double spend?
								LOG_INFO(log) << "BlockCache::Validate BSB status: "
										<< ProcessResultToString(status.reason)
										<< " block hash " << block.Hash();
								//Throw the block out, otherwise it blocks the rest.
								e->bsbs[bsb_idx].pop_front();
								break;
						}
					}
				}
			}

			bool mbs_empty = e->mbs.empty();
			bool last_mb = false;
			while(!e->mbs.empty())
			{
				ApprovedMB & block = *(e->mbs.front());
				ValidationStatus status;
				if(mb_handler.Validate(block, &status))
				{
					mb_handler.ApplyUpdates(block, block.primary_delegate);
					last_mb = block.last_micro_block;
					e->mbs.pop_front();
					if(last_mb) assert(e->mbs.empty());
				}
				else
				{
					LOG_INFO(log) << "BlockCache::Validate MB status: "
								<< ProcessResultToString(status.reason)
								<< " block hash " << block.Hash();
				}
			}

			bool eb_stored = false;
			if((last_mb || mbs_empty) && e->eb != nullptr)
			{
				ApprovedEB & block = *e->eb;
				ValidationStatus status;
				if(eb_handler.Validate(block, &status))
				{
					eb_handler.ApplyUpdates(block, block.primary_delegate);
					LOG_INFO(log) << "BlockCache::Validated EB, block hash: " << block.Hash();
					epochs.pop_front();
					eb_stored = true;
				}
			}

			bool last_epoch_begin = epochs.size()==1 && epochs.front().mbs.empty();
			if( (!eb_stored) && (!last_epoch_begin))
				break;
		}
    }

    NonDelPersistenceManager<ECT> eb_handler;
    NonDelPersistenceManager<MBCT> mb_handler;
    NonDelPersistenceManager<BSBCT> bsb_handler;

    std::mutex mutex;
    Log log;
};

//TODO detect double spend

//    Epoch * FindEpoch(uint32_t epoch_num)
//    {
//    	for(auto bi = epochs.rbegin(); bi != epochs.rend(); ++ bi)
//    	{
//    		if(bi->epoch_num == epoch_num)
//    			return bi;
//    		else if (bi->epoch_num < epoch_num)
//    			return nullptr;
//    	}
//    	return nullptr;
//    }
//    template<ConsensusType CT>
//    bool VerifyAggSignature(std::shared_ptr<PostCommittedBlock<CT>> block)
//    {
//    	switch (CT) {
//		case ECT:
//			return epoch_handler.VerifyAggSignature(*block);
//			break;
//		case ECT:
//			return micro_handler.VerifyAggSignature(*block);
//			break;
//		case ECT:
//			return bsb_handler.VerifyAggSignature(*block);
//			break;
//		default:
//			break;
//		}
//		return false;
//    }
///*
//    std::vector<std::shared_ptr<ApprovedBSB > > bsb[NUM_DELEGATES]; // Batch State Blocks received.
//    std::vector<std::shared_ptr<ApprovedMB > > micro; // Micro.
//    std::vector<std::shared_ptr<ApprovedEB > > epoch; // Epoch.
//        size_t GetNumBSB();
//    size_t GetNumMB();
//    size_t GetNumEB();
//*/
//
///*
//
//namespace BatchBlock {
//
//inline
//const std::string currentDateTime()
//{
//    time_t     now = time(0);
//    struct tm  tstruct;
//    char       buf[80];
//    tstruct = *localtime(&now);
//    strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);
//
//    return buf;
//}
//
//class validator {
//
//    std::vector<std::shared_ptr<BatchBlock::bulk_pull_response> > bsb[NUMBER_DELEGATES]; // Batch State Blocks received.
//    std::vector<std::shared_ptr<BatchBlock::bulk_pull_response_micro> > micro; // Micro.
//    std::vector<std::shared_ptr<BatchBlock::bulk_pull_response_epoch> > epoch; // Epoch.
//    std::shared_ptr<BatchBlock::tips_response> tips;
//
//    static constexpr int NR_BLOCKS = 4096; // Max blocks we wait for before processing.
//
//    logos::node *node;
//
//    uint64_t nr_blocks;
//    uint64_t nextMicro;
//    uint64_t nextEpoch;
//
//    std::mutex mutex;
//
//    // For validation of epoch/micro blocks.
//    shared_ptr<NonDelPersistenceManager<ECT> >  epoch_handler;
//    shared_ptr<NonDelPersistenceManager<MBCT> > micro_handler;
//
//    static int constexpr MAX_RETRY                          = 1000;
//
//    public:
//
//    static constexpr int TIMEOUT = 10; // seconds.
//    static void timeout_s(BatchBlock::validator *this_l);
//
//    /// Class constructor
//    /// @param node pointer (needed to access logging, etc)
//    validator(logos::node *n);
//
//    /// Class destructor
//    virtual ~validator();
//
//    bool can_proceed() // logical //TODO
//    {
//        //return true; // RGD
//        std::lock_guard<std::mutex> lock(mutex);
//        return true; // Hack...
//        if(micro.size() <= 3) { // Was micro.empty()
//            return true; // No pending micros need to be processed, ok to continue...
//        } else {
//            std::cout << "can_proceed:: micro.size(): " << micro.size() << std::endl;
//            for(int i = 0; i < micro.size(); ++i) {
//                std::cout << "can_proceed:: blocked on hash: " << micro[i]->micro->Hash().to_string() << std::endl;
//            }
//            return false;
//        }
//    }
//
//    /// add_tips_response
//    /// Store the tip response in a given request
//    void add_tips_response(std::shared_ptr<BatchBlock::tips_response> &resp)
//    {
//        std::lock_guard<std::mutex> lock(mutex); // DEBUG Might need additional locking in this class...
//        tips = resp;
//    }
//
//    /// add_micro_block callback where a new micro block arriving from
//    ///                 the network is saved to the queue
//    /// @param m shared pointer of bulk_pull_response_micro
//    void add_micro_block(std::shared_ptr<logos::bootstrap_attempt> &attempt, std::shared_ptr<BatchBlock::bulk_pull_response_micro> &m);
//    void request_micro_block(std::shared_ptr<logos::bootstrap_attempt> &attempt, std::shared_ptr<BatchBlock::bulk_pull_response_micro> &m);
//
//    /// add_epoch_block callback where a new epoch block arriving from
//    ///                 the network is saved to the queue
//    /// @param e shared pointer of bulk_pull_response_epoch
//    void add_epoch_block(std::shared_ptr<logos::bootstrap_attempt> &attempt, std::shared_ptr<BatchBlock::bulk_pull_response_epoch> &e);
//
//    /// clear_micro_block clears our micro block queue
//    void clear_micro_block()
//    {
//        std::lock_guard<std::mutex> lock(mutex); // DEBUG Might need additional locking in this class...
//        micro.clear();
//    }
//
//    /// clear_epoch_block clears our epoch block queue
//    void clear_epoch_block()
//    {
//        std::lock_guard<std::mutex> lock(mutex); // DEBUG Might need additional locking in this class...
//        epoch.clear();
//    }
//
//    // Report what we have in our queues so we don't re-request the same blocks...
//
//    /// in_memory_bsb_tips the current bsb tips in memory (not in database yet)
//    std::map<int, std::pair<int64_t, BlockHash> >  in_memory_bsb_tips();//all the blocks in the cache
//
//    /// in_memory_micro_tips the current micro block tips in memory (not in database yet)
//    std::pair<int64_t, BlockHash> in_memory_micro_tips();
//
//    /// in_memory_epoch_tips the current epoch block tips in memory (not in database yet)
//    std::pair<int64_t, BlockHash> in_memory_epoch_tips();
//
//    /// is_black_list_error
//    /// @param result of validation of a block
//    /// @returns true if the result implies we blacklist the peer
//    bool is_black_list_error(ValidationStatus & result)//TODO error type
//    {
//#if 0
//        if(result.reason == logos::process_result::progress         ||
//           result.reason == logos::process_result::old              ||
//           result.reason == logos::process_result::gap_previous     ||
//           result.reason == logos::process_result::gap_source       ||
//           result.reason == logos::process_result::buffered         ||
//           result.reason == logos::process_result::buffering_done   ||
//           result.reason == logos::process_result::pending          ||
//           result.reason == logos::process_result::fork
//        ) {
//            return false; // Allow...
//        }
//        return true; // Fail others...
//#endif
//        return false; // TODO This can't be right, we have no way of knowing why...
//    }
//
//    /// validate validation logic for bsb, micro and epoch
//    /// @param block shared pointer of bulk_pull_response
//    /// @returns boolean (true if failed, false if success)
//    bool validate(std::shared_ptr<logos::bootstrap_attempt> attempt, std::shared_ptr<bulk_pull_response> block);
//
//};
//
//} // namespace BatchBlock
//*/
