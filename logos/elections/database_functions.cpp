#include <logos/elections/database_functions.hpp>
#include <logos/lib/log.hpp>
#include <logos/elections/database.hpp>
#include <logos/blockstore.hpp>
#include <logos/lib/epoch_time_util.hpp>


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





template class FixedSizeHeap<int>;

