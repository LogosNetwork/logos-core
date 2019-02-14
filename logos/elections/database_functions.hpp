#pragma once
#include <logos/blockstore.hpp>

std::vector<std::pair<AccountAddress,uint64_t>> getElectionWinners(size_t num_winners, logos::block_store& store);


//TODO: make some of these private
//want to be able to unit test each of them individually
//but only want clients to call the last one
bool updateCandidatesDB(logos::block_store& store);
bool markDelegateElectsAsRemove(logos::block_store& store);
bool addReelectionCandidates(logos::block_store& store);
bool transitionCandidatesDBNextEpoch(logos::block_store& store);

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
