#pragma once

#include <stdint.h>

#include <list>
#include <unordered_set>
#include <unordered_map>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>

#include <logos/lib/numbers.hpp>
#include <logos/lib/hash.hpp>
#include <logos/consensus/messages/byte_arrays.hpp>
#include <logos/consensus/messages/common.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/persistence/persistence.hpp>

#include "block_write_queue.hpp"

namespace logos
{
using boost::multi_index::indexed_by;
using boost::multi_index::sequenced;
using boost::multi_index::hashed_unique;
using boost::multi_index::identity;

class PendingBlockContainer
{
public:
    using RBPtr = std::shared_ptr<ApprovedRB>;
    using MBPtr = std::shared_ptr<ApprovedMB>;
    using EBPtr = std::shared_ptr<ApprovedEB>;

    using RelianceSet = std::unordered_set<BlockHash>;
    using BlockHashSearchQueue =
    boost::multi_index_container<
        BlockHash,
        indexed_by<
                sequenced<>,
                hashed_unique<
                        identity<BlockHash> >
        >
    >;
    static constexpr uint Max_Recent_DB_Writes = 512;

    struct PendingRB {
        PendingRB()
            : block()
            , reliances()
            , lock(false)
            , direct_write(false)
        {
        }

        PendingRB(const RBPtr &block_, bool verified)
            : block(block_)
            , reliances()
            , lock(false)
            , direct_write(verified)
        {
        }

        RBPtr               block;
        ValidationStatus    status;
        RelianceSet         reliances;          /* revalidation if empty */
        bool                lock;               /* true if some thread validates it now */
        bool                direct_write;       /* true if already verified by consensus logic */
    };

    struct PendingMB {
        PendingMB()
            : block()
            , reliances()
            , lock(false)
            , direct_write(false)
        {
        }

        PendingMB(const MBPtr &block_, bool verified)
            : block(block_)
            , reliances()
            , lock(false)
            , direct_write(verified)
        {
        }

        MBPtr               block;
        ValidationStatus    status;
        RelianceSet         reliances;          /* revalidation if empty */
        bool                lock;               /* true if some thread validates it now */
        bool                direct_write;       /* true if already verified by consensus logic */
    };

    struct PendingEB {
        PendingEB()
            : block()
            , reliances()
            , lock(false)
            , direct_write(false)
        {
        }

        PendingEB(const EBPtr &block_, bool verified)
            : block(block_)
            , reliances()
            , lock(false)
            , direct_write(verified)
        {
        }

        EBPtr               block;
        ValidationStatus    status;
        RelianceSet         reliances;          /* revalidation if empty */
        bool                lock;               /* true if some thread validates it now */
        bool                direct_write;       /* true if already verified by consensus logic */
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

        bool empty()
        {
            if (eb != nullptr)
                return false;
            if ( ! mbs.empty())
                return false;
            for(auto &x :rbs)
            {
                if( ! x.empty())
                    return false;
            }
            return true;
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
    bool IsBlockCachedOrQueued(const BlockHash &hash);

    bool BlockExistsAdd(EBPtr block);
    bool BlockExistsAdd(MBPtr block);
    bool BlockExistsAdd(RBPtr block);

    void BlockDelete(const BlockHash &hash);

    bool AddEpochBlock(EBPtr block, bool verified);
    bool AddMicroBlock(MBPtr block, bool verified);
    bool AddRequestBlock(RBPtr block, bool verified);

    bool AddHashDependency(const BlockHash &hash, ChainPtr ptr);

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

private:
    bool DeleteHashDependencies(const BlockHash &hash, std::list<ChainPtr> &chains);
    void MarkForRevalidation(const BlockHash &hash, std::list<ChainPtr> &chains);
    bool DeleteDependenciesAndMarkForRevalidation(const BlockHash &hash);

    void DumpCachedBlocks();
    void DumpChainTips();

    BlockWriteQueue &                               _write_q;
    std::list<EpochPeriod>                          _epochs;
    std::unordered_set<BlockHash>                   _cached_blocks;
    std::multimap<BlockHash, ChainPtr>              _hash_dependency_table;
    std::mutex                                      _chains_mutex;
    std::mutex                                      _cache_blocks_mutex;
    std::mutex                                      _hash_dependency_table_mutex;
    Log                                             _log;

    /*
     * We use the _recent_DB_writes to record the hashes of blocks recently written
     * to the DB. We need it to deal with a race condition between two threads, one
     * try to add a hash to the _hash_dependency_table basing on out dated information
     * and the other try to clear the same hash from the table.
     *
     * This is a quick and dirty fix. Alternative solutions such as lock the BlockCache
     * (together with block validation) with a single lock, or develop a proper read cache
     * will kill the performance or take too long to finish.
     */
    BlockHashSearchQueue                            _recent_DB_writes;
};

}
