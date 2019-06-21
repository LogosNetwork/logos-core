#include "block_container.hpp"

namespace logos {

bool PendingBlockContainer::BlockExists(EBPtr block)
{
    std::lock_guard<std::mutex> lck (cache_blocks_mutex);
    BlockHash hash = block->Hash();
    bool res = cached_blocks.find(hash) != cached_blocks.end() || write_q.BlockExists(block);
    if (!res)
        cached_blocks.insert(hash);
    return res;
}

bool PendingBlockContainer::BlockExists(MBPtr block)
{
    std::lock_guard<std::mutex> lck (cache_blocks_mutex);
    BlockHash hash = block->Hash();
    bool res = cached_blocks.find(hash) != cached_blocks.end() || write_q.BlockExists(block);
    if (!res)
        cached_blocks.insert(hash);
    return res;
}

bool PendingBlockContainer::BlockExists(RBPtr block)
{
    std::lock_guard<std::mutex> lck (cache_blocks_mutex);
    BlockHash hash = block->Hash();
    bool res = cached_blocks.find(hash) != cached_blocks.end() || write_q.BlockExists(block);
    if (!res)
        cached_blocks.insert(hash);
    return res;
}

bool PendingBlockContainer::AddEpochBlock(EBPtr block)
{
    EPtr ptr = std::make_shared<PendingEB>(block);
    bool found = false;
    std::lock_guard<std::mutex> lck (chains_mutex);

    for (auto bi = epochs.rbegin(); bi != epochs.rend(); ++ bi)
    {
	if (bi->epoch_num == block->epoch_number)
	{
	    //duplicate
	    if (bi->eb == nullptr)
	    {
		bi->eb = ptr;
	    }
	    found = true;
	    break;
	}
	else if (bi->epoch_num < block->epoch_number)
	{
	    epochs.emplace(bi.base(), EpochPeriod(ptr));
	    found = true;
	    break;
	}
    }

    if (!found)
    {
	epochs.emplace_front(EpochPeriod(ptr));
    }

    return !found;
}

bool PendingBlockContainer::AddMicroBlock(MBPtr block)
{
    MPtr ptr = std::make_shared<PendingMB>(block);
    bool add2begin = false;
    bool found = false;
    std::lock_guard<std::mutex> lck (chains_mutex);

    for (auto bi = epochs.rbegin(); bi != epochs.rend(); ++ bi)
    {
	if (bi->epoch_num == block->epoch_number)
	{
	    for (auto mbi = bi->mbs.rbegin(); mbi != bi->mbs.rend(); ++ mbi)
	    {
		if ((*mbi)->block->sequence == block->sequence)
		{
		    //duplicate
		    found = true;
		    break;
		}
		else if ((*mbi)->block->sequence < block->sequence)
		{
		    bi->mbs.emplace(mbi.base(), ptr);

		    found = true;
		    break;
		}
	    }
	    if (!found)
	    {
		bi->mbs.emplace_front(ptr);

		found = true;
		auto temp_i = bi;
		add2begin = ++temp_i == epochs.rend();
	    }
	    break;
	}
	else if (bi->epoch_num < block->epoch_number)
	{
	    epochs.emplace(bi.base(), EpochPeriod(ptr));
	    add2begin = false;
	    found = true;
	    break;
	}
    }

    if (!found)
    {
	epochs.emplace_front(EpochPeriod(ptr));
	found = true;
	add2begin = true;
    }

    return add2begin;
}

bool PendingBlockContainer::AddRequestBlock(RBPtr block)
{
    RPtr ptr = std::make_shared<PendingRB>(block);
    bool add2begin = false;
    bool found = false;
    std::lock_guard<std::mutex> lck (chains_mutex);

    for (auto bi = epochs.rbegin(); bi != epochs.rend(); ++ bi)
    {
	if (bi->epoch_num == block->epoch_number)
	{
	    for (auto rbi = bi->rbs[block->primary_delegate].rbegin();
		    rbi != bi->rbs[block->primary_delegate].rend(); ++ rbi)
	    {
		if ((*rbi)->block->sequence == block->sequence)
		{
		    //duplicate
		    found = true;
		    break;
		}
		else if ((*rbi)->block->sequence < block->sequence)
		{
		    bi->rbs[block->primary_delegate].emplace(rbi.base(), ptr);

		    found = true;
		    break;
		}
	    }
	    if (!found)
	    {
		bi->rbs[block->primary_delegate].emplace_front(ptr);

		found = true;
		add2begin = true;
	    }
	    break;
	}
	else if (bi->epoch_num < block->epoch_number)
	{
	    epochs.emplace(bi.base(), EpochPeriod(ptr));
	    found = true;
	    add2begin = true;
	    break;
	}
    }

    if (!found)
    {
	epochs.emplace_front(EpochPeriod(ptr));
	found = true;
	add2begin = true;
    }

    return add2begin;
}

void PendingBlockContainer::AddDependency(const BlockHash &hash, EPtr block)
{
    std::lock_guard<std::mutex> lck (hash_dependency_table_mutex);
    hash_dependency_table.insert(std::make_pair(hash, ChainPtr(block)));
}

void PendingBlockContainer::AddDependency(const BlockHash &hash, MPtr block)
{
    std::lock_guard<std::mutex> lck (hash_dependency_table_mutex);
    hash_dependency_table.insert(std::make_pair(hash, ChainPtr(block)));
}

void PendingBlockContainer::AddDependency(const BlockHash &hash, RPtr block)
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

bool PendingBlockContainer::MarkAsValidated(EBPtr block)
{
    std::lock_guard<std::mutex> lck (cache_blocks_mutex);
    BlockHash hash = block->Hash();
    cached_blocks.erase(hash);
    return DelDependencies(hash);
}

bool PendingBlockContainer::MarkAsValidated(MBPtr block)
{
    std::lock_guard<std::mutex> lck (cache_blocks_mutex);
    BlockHash hash = block->Hash();
    cached_blocks.erase(hash);
    return DelDependencies(hash);
}

bool PendingBlockContainer::MarkAsValidated(RBPtr block)
{
    std::lock_guard<std::mutex> lck (cache_blocks_mutex);
    BlockHash hash = block->Hash();
    cached_blocks.erase(hash);
    bool res = DelDependencies(hash);
    for(uint32_t i = 0; i < block->requests.size(); ++i)
    {
        res |= DelDependencies(block->requests[i]->Hash());
    }
    return res;
}

bool PendingBlockContainer::DumpCachedBlocks()
{
    std::lock_guard<std::mutex> lck (cache_blocks_mutex);
    LOG_TRACE(log) << "BlockCache::"<<__func__<<": cached hashes: " << cached_blocks.size();
    for (auto & h : cached_blocks)
	LOG_TRACE(log) << h.to_string();
}

}
