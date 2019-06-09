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

void PendingBlockContainer::DelDependencies(const BlockHash &hash, std::list<ChainPtr> &bucket)
{
    std::lock_guard<std::mutex> lck (hash_dependency_table_mutex);

    if (!hash_dependency_table.count(hash))
    {
        return;
    }

    auto range = hash_dependency_table.equal_range(hash);

    for (auto it = range.first; it != range.second; it++)
    {
        bucket.push_back((*it).second);
    }

    hash_dependency_table.erase(range.first, range.second);
}

}
