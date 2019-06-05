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

    BlockWriteQueue(Store &store);

    bool VerifyAggSignature(EBPtr block);
    bool VerifyAggSignature(MBPtr block);
    bool VerifyAggSignature(RBPtr block);

    bool VerifyContent(EBPtr block, ValidationStatus *status);
    bool VerifyContent(MBPtr block, ValidationStatus *status);
    bool VerifyContent(RBPtr block, ValidationStatus *status);

    bool BlockExists(EBPtr block);
    bool BlockExists(MBPtr block);
    bool BlockExists(RBPtr block);

    void StoreBlock(EBPtr block);
    void StoreBlock(MBPtr block);
    void StoreBlock(RBPtr block);

private:
    std::queue<EBPtr>               ebs;
    std::queue<MBPtr>               mbs;
    std::queue<RBPtr>               rbs;

    NonDelPersistenceManager<ECT>   eb_handler;
    NonDelPersistenceManager<MBCT>  mb_handler;
    NonDelPersistenceManager<R>     rb_handler;

    std::mutex                      eqmutex;
    std::mutex                      mqmutex;
    std::mutex                      rqmutex;
};

}
