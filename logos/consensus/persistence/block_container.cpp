#include "block_container.hpp"

namespace logos {

bool PendingBlockContainer::IsBlockCached(const BlockHash &hash)
{
    bool in;
    {
        std::lock_guard<std::mutex> lck(_cache_blocks_mutex);
        in = _cached_blocks.find(hash) != _cached_blocks.end();

#ifdef DUMP_CACHED_BLOCKS
        DumpCachedBlocks();
    }
    {
        std::lock_guard<std::mutex> lck (_chains_mutex);
        DumpChainTips();
#endif
    }
    return in;
}

bool PendingBlockContainer::IsBlockCachedOrQueued(const BlockHash &hash)
{
    std::lock_guard<std::mutex> lck(_cache_blocks_mutex);
    return _cached_blocks.find(hash) != _cached_blocks.end() || _write_q.IsBlockQueued(hash);
}

bool PendingBlockContainer::BlockExistsAdd(EBPtr block)
{
    BlockHash hash = block->Hash();
    std::lock_guard<std::mutex> lck (_cache_blocks_mutex);
    bool res = _cached_blocks.find(hash) != _cached_blocks.end() || _write_q.BlockExists(block);
    if (!res)
    {
        _cached_blocks.insert(hash);
    }
    return res;
}

bool PendingBlockContainer::BlockExistsAdd(MBPtr block)
{
    BlockHash hash = block->Hash();
    std::lock_guard<std::mutex> lck (_cache_blocks_mutex);
    bool res = _cached_blocks.find(hash) != _cached_blocks.end() || _write_q.BlockExists(block);
    if (!res)
    {
        _cached_blocks.insert(hash);
    }
    return res;
}

bool PendingBlockContainer::BlockExistsAdd(RBPtr block)
{
    BlockHash hash = block->Hash();
    std::lock_guard<std::mutex> lck (_cache_blocks_mutex);
    bool res = _cached_blocks.find(hash) != _cached_blocks.end() || _write_q.BlockExists(block);
    if (!res)
    {
        _cached_blocks.insert(hash);
    }
    return res;
}

void PendingBlockContainer::BlockDelete(const BlockHash &hash)
{
    std::lock_guard<std::mutex> lck (_cache_blocks_mutex);
    _cached_blocks.erase(hash);
}

void PendingBlockContainer::DumpCachedBlocks()
{
    //std::lock_guard<std::mutex> lck (_cache_blocks_mutex);
    LOG_TRACE(_log) << "BlockCache:Dump:count: " << _cached_blocks.size();
    for (auto & h : _cached_blocks)
        LOG_TRACE(_log) << "BlockCache:Dump:hash: " << h.to_string();
}

void PendingBlockContainer::DumpChainTips()
{
    //std::lock_guard<std::mutex> lck (_chains_mutex);

    if(! _epochs.empty())
    {
        auto e = _epochs.begin();
        LOG_TRACE(_log) << "BlockCache:DumpChainTips: epoch_num=" << e->epoch_num;
        if(e->eb != nullptr)
        {
            LOG_TRACE(_log) << "BlockCache:DumpChainTips: eb=" << e->eb->block->CreateTip().to_string();
        } else{
            LOG_TRACE(_log) << "BlockCache:DumpChainTips: no eb";
        }

        if(! e->mbs.empty())
        {
            auto m = e->mbs.begin();
            LOG_TRACE(_log) << "BlockCache:DumpChainTips: mb=" << (*m)->block->CreateTip().to_string();
        }else{
            LOG_TRACE(_log) << "BlockCache:DumpChainTips: no mb";
        }

        for (int i = 0; i < NUM_DELEGATES; ++i)
        {
            auto r = e->rbs[i].begin();
            if(r != e->rbs[i].end())
            {
                LOG_TRACE(_log) << "BlockCache:DumpChainTips: rb[" << i << "]=" << (*r)->block->CreateTip().to_string();
            } else{
                LOG_TRACE(_log) << "BlockCache:DumpChainTips: no rb for chain # " << i;
            }
        }
    }
    else {
        LOG_TRACE(_log) << "BlockCache:DumpChainTips: empty";
    }
}

bool PendingBlockContainer::AddEpochBlock(EBPtr block, bool verified)
{
    LOG_TRACE(_log) << "BlockCache:Add:E:{ " << block->CreateTip().to_string();
    EPtr ptr = std::make_shared<PendingEB>(block, verified);
    bool found = false;
    bool need_validate = false;

    std::lock_guard<std::mutex> lck (_chains_mutex);

    for (auto bi = _epochs.rbegin(); bi != _epochs.rend(); ++ bi)
    {
        if (bi->epoch_num == block->epoch_number)
        {
            //duplicate
            if (bi->eb == nullptr)
            {
                if(bi->empty())
                {
                    auto temp_i = bi;
                    need_validate = ++temp_i == _epochs.rend();
                }
                bi->eb = ptr;
            }
            found = true;
            break;
        }
        else if (bi->epoch_num < block->epoch_number)
        {
            _epochs.emplace(bi.base(), EpochPeriod(ptr));
            found = true;
            break;
        }
    }

    if (!found)
    {
        _epochs.emplace_front(EpochPeriod(ptr));
        need_validate = true;
    }

    LOG_TRACE(_log) << "BlockCache:Add:E:} " << (int)need_validate;
    return need_validate;
}

bool PendingBlockContainer::AddMicroBlock(MBPtr block, bool verified)
{
    LOG_TRACE(_log) << "BlockCache:Add:M:{ " << block->CreateTip().to_string();
    MPtr ptr = std::make_shared<PendingMB>(block, verified);
    bool add2begin = false;
    bool found = false;
    std::lock_guard<std::mutex> lck (_chains_mutex);

    for (auto bi = _epochs.rbegin(); bi != _epochs.rend(); ++ bi)
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
                add2begin = ++temp_i == _epochs.rend();
            }
            break;
        }
        else if (bi->epoch_num < block->epoch_number)
        {
            _epochs.emplace(bi.base(), EpochPeriod(ptr));
            add2begin = false;
            found = true;
            break;
        }
    }

    if (!found)
    {
        _epochs.emplace_front(EpochPeriod(ptr));
        found = true;
        add2begin = true;
    }

    LOG_TRACE(_log) << "BlockCache:Add:M:} " << (int)add2begin;
    return add2begin;
}

bool PendingBlockContainer::AddRequestBlock(RBPtr block, bool verified)
{
    LOG_TRACE(_log) << "BlockCache:Add:R:{ " << block->CreateTip().to_string();
    RPtr ptr = std::make_shared<PendingRB>(block, verified);
    bool add2begin = false;
    bool found = false;
    std::lock_guard<std::mutex> lck (_chains_mutex);

    for (auto bi = _epochs.rbegin(); bi != _epochs.rend(); ++ bi)
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
            _epochs.emplace(bi.base(), EpochPeriod(ptr));
            found = true;
            add2begin = true;
            break;
        }
    }

    if (!found)
    {
        _epochs.emplace_front(EpochPeriod(ptr));
        found = true;
        add2begin = true;
    }

    LOG_TRACE(_log) << "BlockCache:Add:R:} " << (int)add2begin;
    return add2begin;
}

bool PendingBlockContainer::AddHashDependency(const BlockHash &hash, ChainPtr ptr)
{
    LOG_TRACE(_log) << "BlockCache:AddHashDependency " << hash.to_string();
    {
        std::lock_guard<std::mutex> lck (_hash_dependency_table_mutex);
        if(_recent_DB_writes. template get<1>().find(hash) != _recent_DB_writes. template get<1>().end())
        {
            LOG_TRACE(_log) << "BlockCache:AddHashDependency: Dependency is in _recent_DB_writes, hash="
                            << hash.to_string();
            return false;
        }
        _hash_dependency_table.insert(std::make_pair(hash, ptr));
    }
    {
        std::lock_guard<std::mutex> lck (_chains_mutex);
        if (ptr.eptr)
        {
            ptr.eptr->reliances.insert(hash);
        }
        else if (ptr.mptr)
        {
            ptr.mptr->reliances.insert(hash);
        }
        else if (ptr.rptr)
        {
            ptr.rptr->reliances.insert(hash);
        }
    }
    return true;
}

void PendingBlockContainer::MarkForRevalidation(const BlockHash &hash, std::list<ChainPtr> &chains)
{
    LOG_TRACE(_log) << "BlockCache:MarkForRevalidation " << hash.to_string();
    std::lock_guard<std::mutex> lck (_chains_mutex);

    for (auto ptr : chains)
    {
        if (ptr.eptr)
        {
            LOG_TRACE(_log) << "BlockCache:Mark:E:"
                    << ptr.eptr->block->CreateTip().to_string()
                    << " has one less dependency: " << hash.to_string();
            ptr.eptr->reliances.erase(hash);
        }
        else if (ptr.mptr)
        {
            LOG_TRACE(_log) << "BlockCache:Mark:M:"
                    << ptr.mptr->block->CreateTip().to_string()
                    << " has one less dependency: " << hash.to_string();
            ptr.mptr->reliances.erase(hash);
            {
                //TODO remove after integration
                LOG_TRACE(_log) << "BlockCache:Mark # reliance " << ptr.mptr->reliances.size();
                for( auto & h : ptr.mptr->reliances)
                {
                    LOG_TRACE(_log) << "BlockCache:Mark remaining reliance hash " << h.to_string();
                }
            }
        }
        else if (ptr.rptr)
        {
            LOG_TRACE(_log) << "BlockCache:Mark:R:"
                    << ptr.rptr->block->CreateTip().to_string()
                    << " has one less dependency: " << hash.to_string();
            ptr.rptr->reliances.erase(hash);
            {
                //TODO remove after integration
                LOG_TRACE(_log) << "BlockCache:Mark # reliance " << ptr.rptr->reliances.size();
                for( auto & h : ptr.rptr->reliances)
                {
                    LOG_TRACE(_log) << "BlockCache:Mark remaining reliance hash " << h.to_string();
                }
            }
        }
    }
}

bool PendingBlockContainer::DeleteHashDependencies(const BlockHash &hash, std::list<ChainPtr> &chains)
{
    LOG_TRACE(_log) << "BlockCache:DeleteHashDependencies " << hash.to_string();

    std::lock_guard<std::mutex> lck (_hash_dependency_table_mutex);

    _recent_DB_writes.push_back(hash);
    if(_recent_DB_writes.size() > Max_Recent_DB_Writes)
    {
        _recent_DB_writes.pop_front();
    }

    if (!_hash_dependency_table.count(hash))
    {
        return false;
    }

    auto range = _hash_dependency_table.equal_range(hash);

    for (auto it = range.first; it != range.second; it++)
    {
        chains.push_back((*it).second);
    }

    _hash_dependency_table.erase(range.first, range.second);

    return true;
}

bool PendingBlockContainer::DeleteDependenciesAndMarkForRevalidation(const BlockHash &hash)
{
    LOG_TRACE(_log) << "BlockCache:DeleteAndMark, hash " << hash.to_string();

    std::list<ChainPtr> chains;
    bool res = DeleteHashDependencies(hash, chains);
    if (res)
    {
        MarkForRevalidation(hash, chains);
    }
    return res;
}

bool PendingBlockContainer::MarkAsValidated(EBPtr block)
{
    return DeleteDependenciesAndMarkForRevalidation(block->Hash());
}

bool PendingBlockContainer::MarkAsValidated(MBPtr block)
{
    return DeleteDependenciesAndMarkForRevalidation(block->Hash());
}

bool PendingBlockContainer::MarkAsValidated(RBPtr block)
{
    auto block_hash = block->Hash();
    LOG_TRACE(_log) << "BlockCache:MarkAsValidated, hash " << block_hash.to_string();
    bool res = DeleteDependenciesAndMarkForRevalidation(block_hash);
    for (uint32_t i = 0; i < block->requests.size(); ++i)
    {
        auto request = block->requests[i];
        res |= DeleteDependenciesAndMarkForRevalidation(request->Hash());
        res |= DeleteDependenciesAndMarkForRevalidation(request->GetSource());
        switch (request->type)
        {
        case RequestType::Send:
            {
                auto send = dynamic_pointer_cast<const Send>(request);
                for(auto &t : send->transactions)
                {
                    res |= DeleteDependenciesAndMarkForRevalidation(t.destination);
                }
            }
            break;
        case RequestType::Revoke:
            {
                auto revoke = dynamic_pointer_cast<const Revoke>(request);
                res |= DeleteDependenciesAndMarkForRevalidation(revoke->transaction.destination);
            }
            break;
        case RequestType::Distribute:
            {
                auto distribute = dynamic_pointer_cast<const Distribute>(request);
                res |= DeleteDependenciesAndMarkForRevalidation(distribute->transaction.destination);
            }
            break;
        case RequestType::WithdrawFee:
            {
                auto withdraw = dynamic_pointer_cast<const WithdrawFee>(request);
                res |= DeleteDependenciesAndMarkForRevalidation(withdraw->transaction.destination);
            }
            break;
        case RequestType::WithdrawLogos:
            {
                auto withdraw = dynamic_pointer_cast<const WithdrawLogos>(request);
                res |= DeleteDependenciesAndMarkForRevalidation(withdraw->transaction.destination);
            }
            break;
        case RequestType::TokenSend:
            {
                auto send = dynamic_pointer_cast<const TokenSend>(request);
                for(auto &t : send->transactions)
                {
                    res |= DeleteDependenciesAndMarkForRevalidation(t.destination);
                }
            }
            break;
        default:
            break;
        }
    }

    return res;
}

bool PendingBlockContainer::GetNextBlock(ChainPtr &ptr, uint8_t &rb_idx, bool success)
{
    LOG_TRACE(_log) << "BlockCache:Next"
            << ":idx " << (int)rb_idx << ":success " << (int)success;

    uint32_t epoch_number;
    std::lock_guard<std::mutex> lck (_chains_mutex);
    if (ptr.rptr)
    {
        epoch_number = ptr.rptr->block->epoch_number;
        rb_idx = ptr.rptr->block->primary_delegate;
        ptr.rptr->lock = false;
    }
    else if (ptr.mptr)
    {
        epoch_number = ptr.mptr->block->epoch_number;
        ptr.mptr->lock = false;
    }
    else if (ptr.eptr)
    {
        epoch_number = ptr.eptr->block->epoch_number;
        ptr.eptr->lock = false;
    }
    else
    {
        epoch_number = -1u;
    }

    assert(rb_idx<=NUM_DELEGATES);

#ifdef DUMP_CACHED_BLOCKS
    DumpChainTips();
#endif

    auto e = _epochs.begin();
    while (e != _epochs.end())
    {
        if (epoch_number != -1u && e->epoch_num != epoch_number)
        {
            ++e;
            continue;
        }

        epoch_number = -1u;

        if (!ptr.mptr && !ptr.eptr)
        {
            if (ptr.rptr)
            {
                if (success)
                {
                    e->rbs[rb_idx].pop_front();
                }
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
                            || !((*to_validate)->reliances.empty())
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
                        ptr.rptr->lock = true;
                        LOG_TRACE(_log) << "BlockCache:Next:R: "
                                << ptr.rptr->block->CreateTip().to_string();
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
                if (success)
                {
                    e->mbs.pop_front();
                    last_mb = ptr.mptr->block->last_micro_block;
                    if (last_mb)
                        assert(e->mbs.empty());
                }
                ptr.mptr = nullptr;
            }

            if (!e->mbs.empty()
                    && e->mbs.front()->reliances.empty()
                    && !e->mbs.front()->lock)
            {
                ptr.mptr = e->mbs.front();
                ptr.mptr->lock = true;
                LOG_TRACE(_log) << "BlockCache:Next:M: "
                        << ptr.mptr->block->CreateTip().to_string();
                return true;
            }
        }

        if (ptr.eptr)
        {
            ptr.eptr = nullptr;
            if (success)
            {
                e->eb = nullptr;
                auto e_old = e;
                ++e;
                _epochs.erase(e_old);
                continue;
            }
        }

        if ((last_mb			    /* if last microblock in this epoch is validated */
                    || mbs_empty) &&        /*     or there was no unvalidated microblocks in the queue */
                e->eb &&                    /* and unvalidated epoch block is present */
                e->eb->reliances.empty() && /* and epoch block is marked for validation */
                !e->eb->lock)               /* and it is not located by another validation thread */
        {                                   /* then return this epoch block and next for validation */
            ptr.eptr = e->eb;
            ptr.eptr->lock = true;
            LOG_TRACE(_log) << "BlockCache:Next:E: "
                    << ptr.eptr->block->CreateTip().to_string();
            return true;
        }

                                            /* first 10 minutes of new epoch: */
        if (_epochs.size() == 2 &&          /* if there are two unvalidated epochs in the queue */
                e->eb == nullptr &&         /* and unvalidated epoch block for previous epoch is not present */
                e->mbs.empty() &&           /* and there are no unvalidated microblocks for previous epoch */
                ++e != _epochs.end() &&     /* and the next epoch is the last of these two epochs */
                e->mbs.empty())             /* and there are no unvalidated microblocks for next epoch */
        {                                   /* then continue searching blocks for validation in the next epoch */
            continue;
        }
        else                                /* else stop searching blocks for validation */
        {
            break;
        }
    }

    LOG_TRACE(_log) << "BlockCache:Next:end";

    return false;
}

}
