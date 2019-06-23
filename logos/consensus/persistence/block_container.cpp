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

bool PendingBlockContainer::GetNextBlock(ChainPtr &ptr, uint8_t &rb_idx, bool success)
{
    LOG_TRACE(log) << "PendingBlockContainer::"<<__func__<<"{";

    uint32_t epoch_number;

    if (ptr.rptr)
    {
        epoch_number = ptr.rptr->block->epoch_number;
        rb_idx = ptr.rptr->block->primary_delegate;
    }
    else if (ptr.mptr)
    {
        epoch_number = ptr.mptr->block->epoch_number;
    }
    else if (ptr.eptr)
    {
        epoch_number = ptr.eptr->block->epoch_number;
    }
    else
    {
        epoch_number = -1u;
    }

    assert(rb_idx<=NUM_DELEGATES);

    std::lock_guard<std::mutex> lck (chains_mutex);

    auto e = epochs.begin();
    while (e != epochs.end())
    {
        if (epoch_number != -1u && e->epoch_num != epoch_number)
            continue;

        epoch_number = -1u;

        if (!ptr.mptr && !ptr.eptr)
        {
            if (ptr.rptr)
            {
                if (success)
                    e->rbs[rb_idx].pop_front();
                else
                    e->rbs[rb_idx].front()->lock = false;
                ptr.rptr = nullptr;
            }

            uint num_rb_chain_no_progress = 0;
            // try rb chains till num_rb_chain_no_progress reaches NUM_DELEGATES
            while (num_rb_chain_no_progress < NUM_DELEGATES)
            {
                for (;;)
                {
                    std::list<RPtr>::iterator to_validate = e->rbs[rb_idx].begin();
                    if (to_validate == e->rbs[rb_idx].end()
                            || !(*to_validate)->continue_validate
                            || (*to_validate)->lock)
                    {
                        //cannot make progress with empty list
                        num_rb_chain_no_progress++;
                        rb_idx = (rb_idx + 1) % NUM_DELEGATES;
                        break;//for(;;)
                    }
                    else
                    {
                        ptr.rptr = *to_validate;
                        ptr.rptr->continue_validate = false;
                        ptr.rptr->lock = true;
                        return true;
                    }
                }
            }
        }

        bool mbs_empty = e->mbs.empty();
        bool last_mb = false;
        if (!ptr.eptr)
        {
            if (ptr.mptr)
            {
                if (success) {
                    e->mbs.pop_front();
                    last_mb = ptr.mptr->block->last_micro_block;
                    if (last_mb)
                        assert(e->mbs.empty());
                }
                else
                    e->mbs.front()->lock = false;
                ptr.mptr = nullptr;
            }

            if (!e->mbs.empty()
                    && e->mbs.front()->continue_validate
                    && !e->mbs.front()->lock)
            {
                ptr.mptr = e->mbs.front();
                ptr.mptr->continue_validate = false;
                ptr.mptr->lock = true;
                return true;
            }
        }

        bool e_finished = false;

        if (ptr.eptr)
        {
            if (success)
            {
                e->eb = nullptr;
                auto e_old = e;
                ++e;
                epochs.erase(e_old);
                e_finished = true;
            }
            else
                e->eb->lock = false;
            ptr.eptr = nullptr;
        }

        if ((last_mb || mbs_empty) &&
            e->eb && e->eb->continue_validate && !e->eb->lock)
        {
            ptr.eptr = e->eb;
            ptr.eptr->continue_validate = false;
            ptr.eptr->lock = true;
            return true;
        }

        if (e_finished)
        {
            continue;
        }
        else
        {
            break;
#if 0
            //two-tip case, i.e. first 10 minutes of the latest epoch
            bool last_epoch_begin = block_container.epochs.size()==2 &&
                    e->eb == nullptr &&
                    e->mbs.empty() &&
                    (++e)->mbs.empty();
            if(!last_epoch_begin)
            {
                e = block_container.epochs.end();
            }
            else
            {
                //already did ++e
            }
#endif
        }
    }

    LOG_ERROR(log) << "BlockCache::"<<__func__<<"}";

    return false;
}

void PendingBlockContainer::DumpCachedBlocks()
{
    std::lock_guard<std::mutex> lck (cache_blocks_mutex);
    LOG_TRACE(log) << "BlockCache::"<<__func__<<": cached hashes: " << cached_blocks.size();
    for (auto & h : cached_blocks)
	LOG_TRACE(log) << h.to_string();
}

}
