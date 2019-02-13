#include <logos/elections/database_functions.hpp>
#include <logos/elections/database.hpp>
#include <logos/blockstore.hpp>

template <typename T>
class FixedSizeHeap
{
    std::vector<T> storage;
    size_t capacity;
    std::function<bool(const T&,const T&)> cmp;
    public:
    FixedSizeHeap(size_t capacity,std::function<bool(const T&,const T&)> cmp) 
        : capacity(capacity), cmp(cmp)
    {
        storage.reserve(capacity);
        std::make_heap(storage.begin(),storage.end(),cmp);
    }

    void try_push(T& item)
    {
        storage.push_back(item);
        std::push_heap(storage.begin(),storage.end(),cmp);
        if(storage.size() > capacity)
        {
            std::pop_heap(storage.begin(),storage.end(),cmp);
            storage.pop_back();
        }
    }

    const std::vector<T>& getStorage()
    {
        return storage;
    }
};

std::vector<std::pair<AccountAddress,uint64_t>> getElectionWinners(
        size_t num_winners,
        logos::block_store& store)
{

    logos::transaction txn(store.environment, nullptr, false);
    FixedSizeHeap<std::pair<AccountAddress,uint64_t>> results(num_winners,
            [](auto p1,auto p2)
            {
                return p1.second < p2.second;
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
    return results.getStorage();
}

