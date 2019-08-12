#pragma once

#include <stdint.h>

#include <list>
#include <unordered_set>
#include <unordered_map>

#include <logos/lib/numbers.hpp>
#include <logos/lib/hash.hpp>
#include <logos/consensus/messages/byte_arrays.hpp>
#include <logos/consensus/messages/common.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/persistence/persistence.hpp>

#include "block_write_queue.hpp"

namespace logos
{

class PendingBlockContainer
{
public:
    using RBPtr = std::shared_ptr<ApprovedRB>;
    using MBPtr = std::shared_ptr<ApprovedMB>;
    using EBPtr = std::shared_ptr<ApprovedEB>;

    using RelianceSet = std::unordered_set<BlockHash>;

    struct PendingRB {
        PendingRB()
            : block()
            , reliances()
            , lock(false)
        {
        }

        PendingRB(const RBPtr &block_)
            : block(block_)
            , reliances()
            , lock(false)
        {
        }

        RBPtr               block;
        ValidationStatus    status;
        RelianceSet         reliances;          /* revalidation if empty */
        bool                lock;               /* true if some thread validates it now */
    };

    struct PendingMB {
        PendingMB()
            : block()
            , reliances()
            , lock(false)
        {
        }

        PendingMB(const MBPtr &block_)
            : block(block_)
            , reliances()
            , lock(false)
        {
        }

        MBPtr               block;
        ValidationStatus    status;
        RelianceSet         reliances;          /* revalidation if empty */
        bool                lock;               /* true if some thread validates it now */
    };

    struct PendingEB {
        PendingEB()
            : block()
            , reliances()
            , lock(false)
        {
        }

        PendingEB(const EBPtr &block_)
            : block(block_)
            , reliances()
            , lock(false)
        {
        }

        EBPtr               block;
        ValidationStatus    status;
        RelianceSet         reliances;          /* revalidation if empty */
        bool                lock;               /* true if some thread validates it now */
    };

    using RPtr = std::shared_ptr<PendingRB>;
    using MPtr = std::shared_ptr<PendingMB>;
    using EPtr = std::shared_ptr<PendingEB>;

    struct EpochPeriod
    {
        EpochPeriod(uint32_t epoch_num)
            : epoch_num(epoch_num)
            , eb(nullptr)
        {
        }
        EpochPeriod(EPtr block)
            : epoch_num(block->block->epoch_number)
            , eb(block)
        {
        }
        EpochPeriod(MPtr block)
            : epoch_num(block->block->epoch_number)
            , eb(nullptr)
        {
            mbs.push_front(block);
        }
        EpochPeriod(RPtr block)
            : epoch_num(block->block->epoch_number)
            , eb(nullptr)
        {
            assert(block->block->primary_delegate < NUM_DELEGATES);
            rbs[block->block->primary_delegate].push_front(block);
        }

        uint32_t                        epoch_num;
        EPtr                            eb;
        std::list<MPtr>                 mbs;
        std::list<RPtr>                 rbs[NUM_DELEGATES];
    };

    struct ChainPtr
    {
        /* This is an union by design. Only one of these pointers is non-empty.
         * But union is not applicable here because of non-trivial destructor of shared_ptr type.
         */
        RPtr rptr;
        MPtr mptr;
        EPtr eptr;

        ChainPtr()
        {
        }
        ChainPtr(RPtr r)
            : rptr(r)
        {
        }
        ChainPtr(MPtr m)
            : mptr(m)
        {
        }
        ChainPtr(EPtr e)
            : eptr(e)
        {
        }
    };

    PendingBlockContainer(BlockWriteQueue &write_q)
        : _write_q(write_q)
    {
    }

    bool IsBlockCached(const BlockHash &hash);

    bool BlockExistsAdd(EBPtr block);
    bool BlockExistsAdd(MBPtr block);
    bool BlockExistsAdd(RBPtr block);

    void BlockDelete(const BlockHash &hash);

    bool AddEpochBlock(EBPtr block);
    bool AddMicroBlock(MBPtr block);
    bool AddRequestBlock(RBPtr block);

    void AddHashDependency(const BlockHash &hash, ChainPtr ptr);

    bool MarkAsValidated(EBPtr block);
    bool MarkAsValidated(MBPtr block);
    bool MarkAsValidated(RBPtr block);

    /*
     * Returns true if next block for validation exists in queues; it returned via ptr pointer.
     * Returns false if no blocks avaliable for validation.
     * The performing the next call, values of ptr and rb_idx must be the same as returned by previous call.
     * The success parameter must contain the result of validation of the returned block in the previous step.
     */
    bool GetNextBlock(ChainPtr &ptr, uint8_t &rb_idx, bool success);

    void DumpCachedBlocks();
    void DumpChainTips();

private:
    bool DeleteHashDependencies(const BlockHash &hash, std::list<ChainPtr> &chains);
    void MarkForRevalidation(const BlockHash &hash, std::list<ChainPtr> &chains);
    bool DeleteDependenciesAndMarkForRevalidation(const BlockHash &hash);

    BlockWriteQueue &                               _write_q;
    std::list<EpochPeriod>                          _epochs;
    std::unordered_set<BlockHash>                   _cached_blocks;
    std::multimap<BlockHash, ChainPtr>              _hash_dependency_table;
    std::mutex                                      _chains_mutex;
    std::mutex                                      _cache_blocks_mutex;
    std::mutex                                      _hash_dependency_table_mutex;
    Log                                             _log;
};

}
