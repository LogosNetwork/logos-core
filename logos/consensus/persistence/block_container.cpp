#include "block_container.hpp"

namespace logos {

void PendingBlockContainer::AddDependency(const BlockHash &hash, EBPtr block)
{
    std::lock_guard<std::mutex> lck (hash_dependency_table_mutex);
    hash_dependency_table.insert(std::make_pair(hash, ChainPtr(block)));
}

void PendingBlockContainer::AddDependency(const BlockHash &hash, MBPtr block)
{
    std::lock_guard<std::mutex> lck (hash_dependency_table_mutex);
    hash_dependency_table.insert(std::make_pair(hash, ChainPtr(block)));
}

void PendingBlockContainer::AddDependency(const BlockHash &hash, RBPtr block)
{
    std::lock_guard<std::mutex> lck (hash_dependency_table_mutex);
    hash_dependency_table.insert(std::make_pair(hash, ChainPtr(block)));
}

PendingBlockContainer::EpochPeriod *PendingBlockContainer::GetEpoch(uint32_t epoch_num)
{
    for (auto e = epochs.begin(); e != epochs.end(); e++)
    {
        if (e->epoch_num == epoch_num)
            return &*e;
    }
    return 0;
}

bool PendingBlockContainer::MarkForRevalidation(const ChainPtr &ptr)
{
    if (ptr.eptr)
    {
        ptr.eptr->continue_validate = true;
        return true;
    }
    else if (ptr.mptr)
    {
        ptr.mptr->continue_validate = true;
        return true;
    }
    else if (ptr.rptr)
    {
        ptr.rptr->continue_validate = true;
        return true;
    }
    return false;
}

bool PendingBlockContainer::DelDependencies(const BlockHash &hash)
{
    bool res = false;
    std::lock_guard<std::mutex> lck (hash_dependency_table_mutex);

    if (!hash_dependency_table.count(hash))
    {
        return false;
    }

    auto range = hash_dependency_table.equal_range(hash);

    for (auto it = range.first; it != range.second; it++)
    {
        res |= MarkForRevalidation((*it).second);
    }

    hash_dependency_table.erase(range.first, range.second);

    return res;
}

}
