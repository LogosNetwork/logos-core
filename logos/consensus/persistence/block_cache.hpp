#pragma once

#include <memory>
#include <mutex>
#include <unordered_set>

#include <logos/lib/numbers.hpp>
#include <logos/lib/blocks.hpp>
#include <logos/lib/trace.hpp>

#include <logos/consensus/messages/common.hpp>
#include <logos/consensus/messages/messages.hpp>

#include <logos/epoch/epoch.hpp>
#include <logos/epoch/epoch_handler.hpp>
#include <logos/microblock/microblock.hpp>
#include <logos/microblock/microblock_handler.hpp>

#include <logos/consensus/persistence/persistence.hpp>
#include <logos/consensus/persistence/persistence_manager.hpp>
#include <logos/consensus/persistence/epoch/epoch_persistence.hpp>
#include <logos/consensus/persistence/epoch/nondel_epoch_persistence.hpp>
#include <logos/consensus/persistence/microblock/microblock_persistence.hpp>
#include <logos/consensus/persistence/microblock/nondel_microblock_persistence.hpp>
#include <logos/consensus/persistence/request/request_persistence.hpp>
#include <logos/consensus/persistence/request/nondel_request_persistence.hpp>

class IBlockCache
{
public:
    using BSBPtr = std::shared_ptr<ApprovedRB>;
    using MBPtr = std::shared_ptr<ApprovedMB>;
    using EBPtr = std::shared_ptr<ApprovedEB>;

    /**
     * add an epoch block to the cache
     * @param block the block
     * @return true if the block has good signatures.
     */
    virtual bool AddEB(EBPtr block) = 0;

    /**
     * add a micro block to the cache
     * @param block the block
     * @return true if the block has good signatures.
     */
    virtual bool AddMB(MBPtr block) = 0;

    /**
     * add a request block to the cache
     * @param block the block
     * @return true if the block has good signatures.
     */
    virtual bool AddBSB(BSBPtr block) = 0;

    /**
     * check if a block is cached
     * @param b the hash of the block
     * @return true if the block is in the cache
     */
    virtual bool IsBlockCached(const BlockHash &b) = 0;

    virtual ~IBlockCache() = default;
};

class BlockCache: public IBlockCache
{
public:
	using Store = logos::block_store;

	/**
	 * constructor
	 * @param store the database
	 */
    BlockCache(Store &store);

    /**
     * (inherited) add an epoch block to the cache
     * @param block the block
     * @return true if the block has good signatures.
     */
    bool AddEB(EBPtr block) override;

    /**
     * (inherited) add a micro block to the cache
     * @param block the block
     * @return true if the block has good signatures.
     */
    bool AddMB(MBPtr block) override;

    /**
     * (inherited) add a request block to the cache
     * @param block the block
     * @return true if the block has good signatures.
     */
    bool AddBSB(BSBPtr block) override;

    /**
     * (inherited) check if a block is cached
     * @param b the hash of the block
     * @return true if the block is in the cache
     */
    bool IsBlockCached(const BlockHash &b) override;

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
    	{
    	}
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

    	//TODO optimize
    	//1 for each unprocessed tip of the oldest mb
        //std::bitset<NUM_DELEGATES> mb_dependences;
	};
    std::list<Epoch> epochs;
    std::unordered_set<BlockHash> cached_blocks;

    /*
     * should be called when:
     * (1) a new block is added to the beginning of any chain of the oldest epoch,
     * (2) a new block is added to the beginning of any BSB chain of the newest epoch,
     * 	   in which the first MB has not been received.
     */
    void Validate(uint8_t bsb_idx = 0);

    NonDelPersistenceManager<ECT> eb_handler;
    NonDelPersistenceManager<MBCT> mb_handler;
    NonDelPersistenceManager<R> bsb_handler;

    std::mutex mtx;
    Log log;
};

