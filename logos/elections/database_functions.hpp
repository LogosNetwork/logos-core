#pragma once
#include <logos/blockstore.hpp>
#include <logos/epoch/election_requests.hpp>
#include <logos/lib/epoch_time_util.hpp>

std::vector<std::pair<AccountAddress,uint64_t>> getElectionWinners(size_t num_winners, logos::block_store& store);

const std::chrono::milliseconds VOTING_DOWNTIME(1000 * 60 * 10); //10 minutes


//TODO: make some of these private
//want to be able to unit test each of them individually
//but dont want clients to call some of them
bool updateCandidatesDB(logos::block_store& store);
bool markDelegateElectsAsRemove(logos::block_store& store);
bool addReelectionCandidates(logos::block_store& store);
bool transitionCandidatesDBNextEpoch(logos::block_store& store);
bool isValid(logos::block_store& store, ElectionVote& request, uint32_t cur_epoch_num); 
bool isValid(logos::block_store& store, AnnounceCandidacy& request, uint32_t cur_epoch_num);
bool isValid(logos::block_store& store, RenounceCandidacy& request, uint32_t cur_epoch_num);
bool isOutsideOfEpochBoundary(logos::block_store& store, uint32_t cur_epoch_num);
bool applyElectionVote(logos::block_store& store, ElectionVote& request, uint32_t cur_epoch_num);
bool applyCandidacyRequest(logos::block_store& store, AnnounceCandidacy& request);
bool applyCandidacyRequest(logos::block_store& store, RenounceCandidacy& request);

bool isValid(logos::block_store& store, StartRepresenting& request, uint32_t cur_epoch_num);
bool isValid(logos::block_store& store, StopRepresenting& request, uint32_t cur_epoch_num);
bool applyRequest(logos::block_store& store, StartRepresenting& request, uint32_t cur_epoch_num);
bool applyRequest(logos::block_store& store, StopRepresenting& request, uint32_t cur_epoch_num);

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
