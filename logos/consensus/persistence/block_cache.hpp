#pragma once

#include <memory>
#include <mutex>
#include <unordered_set>

#include <logos/lib/numbers.hpp>
#include <logos/lib/hash.hpp>
#include <logos/lib/blocks.hpp>
#include <logos/lib/trace.hpp>

#include <logos/blockstore.hpp>

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

#include "block_container.hpp"
#include "block_write_queue.hpp"

namespace logos {

class IBlockCache
{
public:
    using RBPtr = std::shared_ptr<ApprovedRB>;
    using MBPtr = std::shared_ptr<ApprovedMB>;
    using EBPtr = std::shared_ptr<ApprovedEB>;

    // should be called by bootstrap and P2P
    /**
     * add an epoch block to the cache
     * @param block the block
     * @return true if the block has good signatures.
     */
    virtual bool AddEpochBlock(EBPtr block) = 0;

    /**
     * add a micro block to the cache
     * @param block the block
     * @return true if the block has good signatures.
     */
    virtual bool AddMicroBlock(MBPtr block) = 0;

    /**
     * add a request block to the cache
     * @param block the block
     * @return true if the block has good signatures.
     */
    virtual bool AddRequestBlock(RBPtr block) = 0;

    // should be called by consensus
    virtual void StoreEpochBlock(EBPtr block) = 0;
    virtual void StoreMicroBlock(MBPtr block) = 0;
    virtual void StoreRequestBlock(RBPtr block) = 0;

    // should be called by bootstrap
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
    bool AddEpochBlock(EBPtr block) override;

    /**
     * (inherited) add a micro block to the cache
     * @param block the block
     * @return true if the block has good signatures.
     */
    bool AddMicroBlock(MBPtr block) override;

    /**
     * (inherited) add a request block to the cache
     * @param block the block
     * @return true if the block has good signatures.
     */
    bool AddRequestBlock(RBPtr block) override;

    void StoreEpochBlock(EBPtr block) override;
    void StoreMicroBlock(MBPtr block) override;
    void StoreRequestBlock(RBPtr block) override;

    /**
     * (inherited) check if a block is cached
     * @param b the hash of the block
     * @return true if the block is in the cache
     */
    bool IsBlockCached(const BlockHash &b) override;

private:

    /*
     * should be called when:
     * (1) a new block is added to the beginning of any chain of the oldest epoch,
     * (2) a new block is added to the beginning of any BSB chain of the newest epoch,
     *        in which the first MB has not been received.
     */
    void Validate(uint8_t bsb_idx = 0);

    block_store &                   store_;
    PendingBlockContainer           block_container;
    BlockWriteQueue                 write_q;

    NonDelPersistenceManager<ECT>   eb_handler;
    NonDelPersistenceManager<MBCT>  mb_handler;
    NonDelPersistenceManager<R>     rb_handler;

    std::mutex                      mtx;
    Log                             log;
};

}
