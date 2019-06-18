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

namespace logos
{

class PendingBlockContainer
{
public:
    using RBPtr = std::shared_ptr<ApprovedRB>;
    using MBPtr = std::shared_ptr<ApprovedMB>;
    using EBPtr = std::shared_ptr<ApprovedEB>;

    struct PendingRB {
        PendingRB()
            : block()
            , continue_validate(true)
        {
        }

        PendingRB(const RBPtr &block_)
            : block(block_)
            , continue_validate(true)
        {
        }

        RBPtr               block;
        ValidationStatus    status;
        bool                continue_validate;
    };

    struct PendingMB {
        PendingMB()
            : block()
            , continue_validate(true)
        {
        }

        PendingMB(const MBPtr &block_)
            : block(block_)
            , continue_validate(true)
        {
        }

        MBPtr               block;
        ValidationStatus    status;
        bool                continue_validate;
    };

    struct PendingEB {
        PendingEB()
            : block()
            , continue_validate(true)
        {
        }

        PendingEB(const EBPtr &block_)
            : block(block_)
            , continue_validate(true)
        {
        }

        EBPtr               block;
        ValidationStatus    status;
        bool                continue_validate;
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
        std::unordered_set<BlockHash>   rbs_next_mb_depend_on;
        std::list<RPtr>                 rbs[NUM_DELEGATES];

        //TODO optimize
        //1 for each unprocessed tip of the oldest mb
        //std::bitset<NUM_DELEGATES> mb_dependences;
    };

    struct ChainPtr
    {
        /* This is actually an union. Only one of these pointers is non-empty.
         * But union is not applicable here because of non-trivial destructor of shared_ptr type.
         */
        RPtr rptr;
        MPtr mptr;
        EPtr eptr;

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

    void AddDependency(const BlockHash &hash, EPtr block);
    void AddDependency(const BlockHash &hash, MPtr block);
    void AddDependency(const BlockHash &hash, RPtr block);

    bool MarkAsValidated(EBPtr block);
    bool MarkAsValidated(MBPtr block);
    bool MarkAsValidated(RBPtr block);

private:
    bool DelDependencies(const BlockHash &hash);
    EpochPeriod *GetEpoch(uint32_t epoch_num);
    bool MarkForRevalidation(const ChainPtr &ptr);

    std::list<EpochPeriod>                          epochs;
    std::set<BlockHash>                             cached_blocks;
    std::multimap<BlockHash, ChainPtr>              hash_dependency_table;
    std::unordered_map<AccountAddress, ChainPtr>    account_dependency_table;
    std::mutex                                      hash_dependency_table_mutex;

    friend class BlockCache;
};

}
