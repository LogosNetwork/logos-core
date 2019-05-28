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

namespace logos
{

class BlockChain;

using ChainPtr = std::shared_ptr<BlockChain>;

class BlockChain
{
    using RBPtr = std::shared_ptr<ApprovedRB>;

private:
    RBPtr       rb;
    ChainPtr    next;
};

class EpochPeriod
{
    using RBPtr = std::shared_ptr<ApprovedRB>;
    using MBPtr = std::shared_ptr<ApprovedMB>;
    using EBPtr = std::shared_ptr<ApprovedEB>;

private:
    uint32_t                        epoch_num;
    EBPtr                           eb;
    std::list<MBPtr>                mbs;
    std::unordered_set<BlockHash>   rbs_next_mb_depend_on;
    std::list<RBPtr>                rbs[NUM_DELEGATES];
};

class PendingBlockContainer
{
private:
    std::list<EpochPeriod>                          epochs;
    std::unordered_set<BlockHash>                   cached_blocks;
    std::unordered_map<BlockHash, ChainPtr>         hash_dependency_table;
    std::unordered_map<AccountAddress, ChainPtr>    account_dependency_table;
};

}
