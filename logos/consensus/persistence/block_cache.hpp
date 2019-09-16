#pragma once

#include <memory>
#include <mutex>
#include <unordered_set>

#include <logos/lib/numbers.hpp>
#include <logos/lib/hash.hpp>
#include <logos/lib/trace.hpp>

#include <logos/blockstore.hpp>

#include <logos/consensus/messages/common.hpp>
#include <logos/consensus/messages/messages.hpp>

#include <logos/epoch/epoch.hpp>
#include <logos/epoch/epoch_handler.hpp>
#include <logos/microblock/microblock.hpp>
#include <logos/microblock/microblock_handler.hpp>

#include "block_container.hpp"
#include "block_write_queue.hpp"

namespace logos {

class IBlockCache
{
public:
    using RBPtr = std::shared_ptr<ApprovedRB>;
    using MBPtr = std::shared_ptr<ApprovedMB>;
    using EBPtr = std::shared_ptr<ApprovedEB>;

    enum add_result
    {
        FAILED,
        EXISTS,
        OK
    };

    // should be called by bootstrap and P2P
    /**
     * add an epoch block to the cache
     * @param block the block
     * @return true if the block has good signatures.
     */
    virtual add_result AddEpochBlock(EBPtr block) = 0;

    /**
     * add a micro block to the cache
     * @param block the block
     * @return true if the block has good signatures.
     */
    virtual add_result AddMicroBlock(MBPtr block) = 0;

    /**
     * add a request block to the cache
     * @param block the block
     * @return true if the block has good signatures.
     */
    virtual add_result AddRequestBlock(RBPtr block) = 0;

    // should be called by consensus
    virtual void StoreEpochBlock(EBPtr block) = 0;
    virtual void StoreMicroBlock(MBPtr block) = 0;
    virtual void StoreRequestBlock(RBPtr block) = 0;

    virtual bool ValidateRequest(
            std::shared_ptr<Request> req,
            uint32_t epoch_num,
            logos::process_return& result) 
    {return false;}

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
    BlockCache(boost::asio::io_service & service, Store & store, std::queue<BlockHash> *unit_test_q = 0);

    /**
     * (inherited) add an epoch block to the cache
     * @param block the block
     * @return true if the block has good signatures.
     */
    add_result AddEpochBlock(EBPtr block) override;

    /**
     * (inherited) add a micro block to the cache
     * @param block the block
     * @return true if the block has good signatures.
     */
    add_result AddMicroBlock(MBPtr block) override;

    /**
     * (inherited) add a request block to the cache
     * @param block the block
     * @return true if the block has good signatures.
     */
    add_result AddRequestBlock(RBPtr block) override;

    void StoreEpochBlock(EBPtr block) override;
    void StoreMicroBlock(MBPtr block) override;
    void StoreRequestBlock(RBPtr block) override;

    virtual bool ValidateRequest(
            std::shared_ptr<Request> req,
            uint32_t epoch_num,
            logos::process_return& result)
    {
        return _write_q.ValidateRequest(req,epoch_num,result);
    }

    /**
     * (inherited) check if a block is cached
     * @param b the hash of the block
     * @return true if the block is in the cache
     */
    bool IsBlockCached(const BlockHash &b) override;

    void ProcessDependencies(EBPtr block);
    void ProcessDependencies(MBPtr block);
    void ProcessDependencies(RBPtr block);
private:

    /*
     * should be called when:
     * (1) a new block is added to the beginning of any chain of the oldest epoch,
     * (2) a new block is added to the beginning of any BSB chain of the newest epoch,
     *        in which the first MB has not been received.
     */
    void Validate(uint8_t bsb_idx = 0);

    block_store &                   _store;
    BlockWriteQueue                 _write_q;
    PendingBlockContainer           _block_container;

    Log                             _log;
};

}
