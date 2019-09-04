#pragma once

#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>

#include <logos/consensus/messages/messages.hpp>

#include <logos/consensus/persistence/persistence.hpp>
#include <logos/consensus/persistence/persistence_manager.hpp>
#include <logos/consensus/persistence/epoch/epoch_persistence.hpp>
#include <logos/consensus/persistence/epoch/nondel_epoch_persistence.hpp>
#include <logos/consensus/persistence/microblock/microblock_persistence.hpp>
#include <logos/consensus/persistence/microblock/nondel_microblock_persistence.hpp>
#include <logos/consensus/persistence/request/request_persistence.hpp>
#include <logos/consensus/persistence/request/nondel_request_persistence.hpp>

namespace logos
{

class BlockCache;

class BlockWriteQueue
{
public:
    using RBPtr = std::shared_ptr<ApprovedRB>;
    using MBPtr = std::shared_ptr<ApprovedMB>;
    using EBPtr = std::shared_ptr<ApprovedEB>;
    using Store = logos::block_store;

    BlockWriteQueue(boost::asio::io_service & service, Store &store, BlockCache *cache = 0, std::queue<BlockHash> *unit_test_q = 0);
    ~BlockWriteQueue();

    bool VerifyAggSignature(EBPtr block);
    bool VerifyAggSignature(MBPtr block);
    bool VerifyAggSignature(RBPtr block);

    bool VerifyContent(EBPtr block, ValidationStatus *status);
    bool VerifyContent(MBPtr block, ValidationStatus *status);
    bool VerifyContent(RBPtr block, ValidationStatus *status);

    bool IsBlockQueued(const BlockHash &hash);

    bool BlockExists(EBPtr block);
    bool BlockExists(MBPtr block);
    bool BlockExists(RBPtr block);

    void StoreBlock(EBPtr block);
    void StoreBlock(MBPtr block);
    void StoreBlock(RBPtr block);

private:
    struct BlockPtr
    {
        RBPtr		rptr;
        MBPtr		mptr;
        EBPtr		eptr;
        BlockHash	hash;

        BlockPtr()
        {
        }

        BlockPtr(RBPtr r)
            : rptr(r)
            , hash(r->Hash())
        {
        }
        BlockPtr(MBPtr m)
            : mptr(m)
            , hash(m->Hash())
        {
        }
        BlockPtr(EBPtr e)
            : eptr(e)
            , hash(e->Hash())
        {
        }
    };

    class Semaphore
    {
    public:
        void notify() {
            std::lock_guard<decltype(_mutex)> lock(_mutex);
            ++_count;
            _condition.notify_one();
        }

        void wait() {
            std::unique_lock<decltype(_mutex)> lock(_mutex);
            while(!_count) // Handle spurious wake-ups.
                _condition.wait(lock);
            --_count;
        }
    private:
        std::mutex              _mutex;
        std::condition_variable _condition;
        unsigned long           _count = 0; // Initialized as locked.
    };

    void StoreBlock(BlockPtr ptr);
    void WriteThread();

    boost::asio::io_service &           _service;
    std::queue<BlockPtr>                _q;
    std::unordered_set<BlockHash>       _q_cache;
    NonDelPersistenceManager<ECT>       _eb_handler;
    NonDelPersistenceManager<MBCT>      _mb_handler;
    NonDelPersistenceManager<R>         _rb_handler;
    std::mutex                          _q_mutex;
    std::atomic<bool>                   _terminate;
    BlockCache *                        _block_cache;
    Semaphore                           _write_sem;
    std::queue<BlockHash> *             _unit_test_q;
    std::unordered_set<BlockHash>       _unit_test_requests;
    std::unordered_set<AccountAddress>  _unit_test_accounts;
    Log                                 _log;
    std::thread                         _write_thread;
};

}
