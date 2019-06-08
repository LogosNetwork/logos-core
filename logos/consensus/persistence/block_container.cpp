#include "block_container.hpp"

namespace logos {

void PendingBlockContainer::AddDependency(const BlockHash &hash, EBPtr block)
{
    hash_dependency_table.insert(std::make_pair(hash, ChainPtr(block)));
}

void PendingBlockContainer::AddDependency(const BlockHash &hash, MBPtr block)
{
    hash_dependency_table.insert(std::make_pair(hash, ChainPtr(block)));
}

void PendingBlockContainer::AddDependency(const BlockHash &hash, RBPtr block)
{
    hash_dependency_table.insert(std::make_pair(hash, ChainPtr(block)));
}

}
