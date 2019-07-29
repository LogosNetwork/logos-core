#include "block_container.hpp"

namespace logos {

bool PendingBlockContainer::IsBlockCached(const BlockHash &hash)
{
    std::lock_guard<std::mutex> lck (_cache_blocks_mutex);
    return _cached_blocks.find(hash) != _cached_blocks.end();
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
    std::lock_guard<std::mutex> lck (_cache_blocks_mutex);
    LOG_TRACE(_log) << "BlockCache:Dump:count: " << _cached_blocks.size();
    for (auto & h : _cached_blocks)
        LOG_TRACE(_log) << "BlockCache:Dump:hash: " << h.to_string();
}

bool PendingBlockContainer::AddEpochBlock(EBPtr block)
{
    LOG_TRACE(_log) << "BlockCache:Add:E:{ " << block->CreateTip().to_string();
    EPtr ptr = std::make_shared<PendingEB>(block);
    bool found = false;
    std::lock_guard<std::mutex> lck (_chains_mutex);

    for (auto bi = _epochs.rbegin(); bi != _epochs.rend(); ++ bi)
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
            _epochs.emplace(bi.base(), EpochPeriod(ptr));
            found = true;
            break;
        }
    }

    if (!found)
    {
        _epochs.emplace_front(EpochPeriod(ptr));
    }

    LOG_TRACE(_log) << "BlockCache:Add:E:} " << (int)!found;
    return !found;
}

bool PendingBlockContainer::AddMicroBlock(MBPtr block)
{
    LOG_TRACE(_log) << "BlockCache:Add:M:{ " << block->CreateTip().to_string();
    MPtr ptr = std::make_shared<PendingMB>(block);
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

bool PendingBlockContainer::AddRequestBlock(RBPtr block)
{
    LOG_TRACE(_log) << "BlockCache:Add:R:{ " << block->CreateTip().to_string();
    RPtr ptr = std::make_shared<PendingRB>(block);
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

void PendingBlockContainer::AddHashDependency(const BlockHash &hash, ChainPtr ptr)
{
    std::lock_guard<std::mutex> lck (_hash_dependency_table_mutex);
    _hash_dependency_table.insert(std::make_pair(hash, ptr));
}

void PendingBlockContainer::AddAccountDependency(const AccountAddress &addr, ChainPtr ptr)
{
    std::lock_guard<std::mutex> lck (_account_dependency_table_mutex);
    _account_dependency_table.insert(std::make_pair(addr, ptr));
}

void PendingBlockContainer::MarkForRevalidation(std::list<ChainPtr> &chains)
{
    std::lock_guard<std::mutex> lck (_chains_mutex);

    for (auto ptr : chains)
    {
        if (ptr.eptr)
        {
            LOG_TRACE(_log) << "BlockCache:Mark:E:"
                    << ptr.eptr->block->CreateTip().to_string();
            ptr.eptr->continue_validate = true;
        }
        else if (ptr.mptr)
        {
            LOG_TRACE(_log) << "BlockCache:Mark:M:"
                    << ptr.mptr->block->CreateTip().to_string();
            ptr.mptr->continue_validate = true;
        }
        else if (ptr.rptr)
        {
            LOG_TRACE(_log) << "BlockCache:Mark:R:"
                    << ptr.rptr->block->CreateTip().to_string();
            ptr.rptr->continue_validate = true;
        }
    }
}

bool PendingBlockContainer::DeleteHashDependencies(const BlockHash &hash, std::list<ChainPtr> &chains)
{
    std::lock_guard<std::mutex> lck (_hash_dependency_table_mutex);

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

bool PendingBlockContainer::DeleteAccountDependencies(const AccountAddress &addr, std::list<ChainPtr> &chains)
{
    std::lock_guard<std::mutex> lck (_account_dependency_table_mutex);

    if (!_account_dependency_table.count(addr))
    {
        return false;
    }

    auto range = _account_dependency_table.equal_range(addr);

    for (auto it = range.first; it != range.second; it++)
    {
        chains.push_back((*it).second);
    }

    _account_dependency_table.erase(range.first, range.second);

    return true;
}

bool PendingBlockContainer::MarkAsValidated(EBPtr block)
{
    BlockHash hash = block->Hash();
    std::list<ChainPtr> chains;
    bool res = DeleteHashDependencies(hash, chains);
    if (res)
    {
        MarkForRevalidation(chains);
    }
    return res;
}

bool PendingBlockContainer::MarkAsValidated(MBPtr block)
{
    BlockHash hash = block->Hash();
    std::list<ChainPtr> chains;
    bool res = DeleteHashDependencies(hash, chains);
    if (res)
    {
        MarkForRevalidation(chains);
    }
    return res;
}

bool PendingBlockContainer::MarkAsValidated(RBPtr block)
{
    BlockHash hash = block->Hash();
    std::list<ChainPtr> chains;
    bool res = DeleteHashDependencies(hash, chains);
    for (uint32_t i = 0; i < block->requests.size(); ++i)
    {
        auto request = block->requests[i];
        res |= DeleteHashDependencies(request->Hash(), chains);
        res |= DeleteAccountDependencies(request->GetSource(), chains);
        switch (request->type)
        {
        case RequestType::Send:
            {
                auto send = dynamic_pointer_cast<const Send>(request);
                for(auto &t : send->transactions)
                {
                    res |= DeleteAccountDependencies(t.destination, chains);
                }
            }
            break;
        case RequestType::Revoke:
            {
                auto revoke = dynamic_pointer_cast<const Revoke>(request);
                res |= DeleteAccountDependencies(revoke->transaction.destination, chains);
            }
            break;
        case RequestType::Distribute:
            {
                auto distribute = dynamic_pointer_cast<const Distribute>(request);
                res |= DeleteAccountDependencies(distribute->transaction.destination, chains);
            }
            break;
        case RequestType::WithdrawFee:
            {
                auto withdraw = dynamic_pointer_cast<const WithdrawFee>(request);
                res |= DeleteAccountDependencies(withdraw->transaction.destination, chains);
            }
            break;
        case RequestType::WithdrawLogos:
            {
                auto withdraw = dynamic_pointer_cast<const WithdrawLogos>(request);
                res |= DeleteAccountDependencies(withdraw->transaction.destination, chains);
            }
            break;
        case RequestType::TokenSend:
            {
                auto send = dynamic_pointer_cast<const TokenSend>(request);
                for(auto &t : send->transactions)
                {
                    res |= DeleteAccountDependencies(t.destination, chains);
                }
            }
            break;
        default:
            break;
        }
    }
    if (res)
    {
        MarkForRevalidation(chains);
    }
    return res;
}

bool PendingBlockContainer::GetNextBlock(ChainPtr &ptr, uint8_t &rb_idx, bool success)
{
    LOG_TRACE(_log) << "BlockCache:Next"
            << ":idx " << (int)rb_idx << ":success " << (int)success;

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

    std::lock_guard<std::mutex> lck (_chains_mutex);

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
                else
                {
                    e->rbs[rb_idx].front()->lock = false;
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
                else
                {
                    e->mbs.front()->lock = false;
                }
                ptr.mptr = nullptr;
            }

            if (!e->mbs.empty()
                    && e->mbs.front()->continue_validate
                    && !e->mbs.front()->lock)
            {
                ptr.mptr = e->mbs.front();
                ptr.mptr->continue_validate = false;
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
            else
            {
                e->eb->lock = false;
            }
        }

        if ((last_mb			    /* if last microblock in this epoch is validated */
                    || mbs_empty) &&        /*     or there was no unvalidated microblocks in the queue */
                e->eb &&                    /* and unvalidated epoch block is present */
                e->eb->continue_validate && /* and epoch block is marked for validation */
                !e->eb->lock)               /* and it is not located by another validation thread */
        {                                   /* then return this epoch block and next for validation */
            ptr.eptr = e->eb;
            ptr.eptr->continue_validate = false;
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
