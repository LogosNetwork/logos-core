#include <logos/elections/database_functions.hpp>
#include <logos/elections/database.hpp>
#include <logos/blockstore.hpp>


template <typename T>
FixedSizeHeap<T>::FixedSizeHeap(size_t capacity,std::function<bool(const T&,const T&)> cmp) 
    : capacity(capacity), cmp(cmp)
{
    storage.reserve(capacity);
    std::make_heap(storage.begin(),storage.end(),cmp);
}

template <typename T>
void FixedSizeHeap<T>::try_push(const T& item)
{
    storage.push_back(item);
    std::push_heap(storage.begin(),storage.end(),cmp);
    if(storage.size() > capacity)
    {
        std::pop_heap(storage.begin(),storage.end(),cmp);
        storage.pop_back();
    }
}

template <typename T>
std::vector<T> FixedSizeHeap<T>::getResults()
{
    std::vector<T> copy(storage);
    std::sort_heap(copy.begin(),copy.end(),cmp);
    return copy;
}

template <typename T>
const std::vector<T>& FixedSizeHeap<T>::getStorage()
{
    return storage;
}

std::vector<std::pair<AccountAddress,uint64_t>> getElectionWinners(
        size_t num_winners,
        logos::block_store& store)
{

    logos::transaction txn(store.environment, nullptr, false);
    FixedSizeHeap<std::pair<AccountAddress,uint64_t>> results(num_winners,
            [](auto p1,auto p2)
            {
                return p1.second > p2.second;
            });
    for(auto it = logos::store_iterator(txn, store.candidacy_db);
           it != logos::store_iterator(nullptr); ++it)
    {
        bool error = false;
        CandidateInfo candidate_info(error,it->second);
        if(!error)
        {
            auto pair = std::make_pair(it->first.uint256(),candidate_info.votes_received_weighted);
            results.try_push(pair);
        } 
    }
    return results.getResults();
}

bool markDelegateElectsAsRemove(logos::block_store& store)
{
    logos::transaction txn(store.environment, nullptr, true);
    BlockHash hash;
    bool res = store.epoch_tip_get(hash,txn);
    if(res)
    {
        return res;
    }
    ApprovedEB epoch;
    res = store.epoch_get(hash,epoch,txn);
    if(res)
    {
        return res;
    }

    for(Delegate& d: epoch.delegates)
    {
        if(d.starting_term)
        {
            if(store.candidate_mark_remove(d.account,txn))
            {
                res = true;
            }
        }
    }
    return res;
}

bool addReelectionCandidates(logos::block_store& store)
{
    logos::transaction txn(store.environment, nullptr, true);
    BlockHash hash;
    if(store.epoch_tip_get(hash,txn))
    {
        return true;
    }
    ApprovedEB epoch;
    if(store.epoch_get(hash,epoch,txn))
    {
        return true;
    }

    size_t num_epochs = 3;
    for(size_t i = 0; i < num_epochs; ++i)
    {
        auto previous_hash = epoch.previous;
        if(previous_hash == 0)
        {
            //not enough epochs have passed
            return false;
        }
        if(store.epoch_get(previous_hash,epoch,txn))
        {
            return true;
        }
    }

    for(auto& d : epoch.delegates)
    {
        if(d.starting_term)
        {
            store.candidate_add_new(d.account,txn);
        }
    }
    return false;
}

bool updateCandidatesDB(logos::block_store& store)
{
    bool res = false;
    logos::transaction txn(store.environment,nullptr,true);
    for(auto it = logos::store_iterator(txn, store.candidacy_db);
            it != logos::store_iterator(nullptr); ++it)
    {
        bool error = false;
        CandidateInfo info(error,it->second);
        if(!error)
        {
            if(info.remove)
            {
                if(mdb_cursor_del(it.cursor,0))
                {
                    res = true;
                }
            }
            else if(!info.active)
            {
                info.active = true;
                std::vector<uint8_t> buf;

                if(mdb_cursor_put(it.cursor,it->first,info.to_mdb_val(buf),MDB_CURRENT))
                {
                    res = true;
                }
            }
        }
        res = res || error;
    }
    return res;
}


bool transitionCandidatesDBNextEpoch(logos::block_store& store)
{
    bool result = false;
    if(addReelectionCandidates(store))
    {
        result = true;
    }
    if(markDelegateElectsAsRemove(store))
    {
        result = true;
    }
    if(updateCandidatesDB(store))
    {
        result = true;
    }
    return result;
}

template class FixedSizeHeap<int>;

