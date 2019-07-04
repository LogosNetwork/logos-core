#pragma once

#include <queue>
#include <mutex>

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

class BlockWriteQueue
{
public:
    using RBPtr = std::shared_ptr<ApprovedRB>;
    using MBPtr = std::shared_ptr<ApprovedMB>;
    using EBPtr = std::shared_ptr<ApprovedEB>;
    using Store = logos::block_store;

    struct BlockPtr
    {
        RBPtr		rptr;
        MBPtr		mptr;
        EBPtr		eptr;
        BlockHash	hash;

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

    BlockWriteQueue(Store &store, std::queue<BlockHash> *unit_test_q = 0);

    bool VerifyAggSignature(EBPtr block);
    bool VerifyAggSignature(MBPtr block);
    bool VerifyAggSignature(RBPtr block);

    bool VerifyContent(EBPtr block, ValidationStatus *status);
    bool VerifyContent(MBPtr block, ValidationStatus *status);
    bool VerifyContent(RBPtr block, ValidationStatus *status);

    bool IsBlockCached(const BlockHash &hash);

    bool BlockExists(EBPtr block);
    bool BlockExists(MBPtr block);
    bool BlockExists(RBPtr block);

    void StoreBlock(EBPtr block);
    void StoreBlock(MBPtr block);
    void StoreBlock(RBPtr block);

private:
    void StoreBlock(BlockPtr ptr);

    std::queue<BlockPtr>            _q;
    std::unordered_set<BlockHash>   _q_cache;
    NonDelPersistenceManager<ECT>   _eb_handler;
    NonDelPersistenceManager<MBCT>  _mb_handler;
    NonDelPersistenceManager<R>     _rb_handler;
    std::mutex                      _q_mutex;
    std::queue<BlockHash> *         _unit_test_q;
    Log                             _log;
};

}
