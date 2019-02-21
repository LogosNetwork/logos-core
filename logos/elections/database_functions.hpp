#pragma once
#include <logos/blockstore.hpp>
#include <logos/elections/requests.hpp>
#include <logos/lib/epoch_time_util.hpp>
#include <unordered_set>










template <typename T>
class FixedSizeHeap
{
    std::vector<T> storage;
    size_t capacity;
    std::function<bool(const T&,const T&)> cmp;
    public:
    FixedSizeHeap(size_t capacity,std::function<bool(const T&,const T&)> cmp);

    void try_push(const T& item);

    std::vector<T> getResults();

    const std::vector<T>& getStorage();
};
