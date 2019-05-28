#pragma once

#include <logos/lib/numbers.hpp>
#include <logos/lib/hash.hpp>
#include <logos/blockstore.hpp>
#include <logos/consensus/messages/messages.hpp>

#include "block_container.hpp"
#include "block_write_queue.hpp"

namespace logos {

class Cache
{
    using RBPtr = std::shared_ptr<ApprovedRB>;
    using MBPtr = std::shared_ptr<ApprovedMB>;
    using EBPtr = std::shared_ptr<ApprovedEB>;

public:
    Cache(block_store & store);

    // should be called by bootstrap and P2P
    bool AddEpochBlock(EBPtr block);
    bool AddMicroBlock(MBPtr block);
    bool AddRequestBlock(RBPtr block);

    // should be called by consensus
    void StoreEpochBlock(EBPtr block);
    void StoreMicroBlock(MBPtr block);
    void StoreRequestBlock(RBPtr block);

    // should be called by bootstrap
    bool IsBlockCached(const BlockHash &b);

private:
    PendingBlockContainer   block_container;
    BlockWriteQueue         write_q;
    block_store &           store_;
};

}
