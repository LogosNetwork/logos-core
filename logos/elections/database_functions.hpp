#pragma once
#include <logos/blockstore.hpp>
#include <logos/epoch/election_requests.hpp>
#include <logos/lib/epoch_time_util.hpp>

std::vector<std::pair<AccountAddress,uint64_t>> getElectionWinners(size_t num_winners, logos::block_store& store, MDB_txn* txn);

const std::chrono::milliseconds VOTING_DOWNTIME(1000 * 60 * 10); //10 minutes


//TODO: make some of these private
//want to be able to unit test each of them individually
//but dont want clients to call some of them
//TODO: maybe make these methods of a struct, where the store and txn are members
bool updateCandidatesDB(logos::block_store& store, MDB_txn* txn);
bool markDelegateElectsAsRemove(logos::block_store& store, MDB_txn* txn);
bool addReelectionCandidates(logos::block_store& store, MDB_txn* txn);
bool transitionCandidatesDBNextEpoch(logos::block_store& store, MDB_txn* txn);
bool isValid(logos::block_store& store, ElectionVote& request, uint32_t cur_epoch_num, MDB_txn* txn); 
bool isValid(logos::block_store& store, AnnounceCandidacy& request, uint32_t cur_epoch_num, MDB_txn* txn);
bool isValid(logos::block_store& store, RenounceCandidacy& request, uint32_t cur_epoch_num, MDB_txn* txn);
bool isOutsideOfEpochBoundary(logos::block_store& store, uint32_t cur_epoch_num, MDB_txn* txn);
bool applyRequest(logos::block_store& store, ElectionVote& request, uint32_t cur_epoch_num, MDB_txn* txn);
bool applyRequest(logos::block_store& store, AnnounceCandidacy& request, MDB_txn* txn);
bool applyRequest(logos::block_store& store, RenounceCandidacy& request, MDB_txn* txn);

bool isValid(logos::block_store& store, StartRepresenting& request, uint32_t cur_epoch_num, MDB_txn* txn);
bool isValid(logos::block_store& store, StopRepresenting& request, uint32_t cur_epoch_num, MDB_txn* txn);
bool applyRequest(logos::block_store& store, StartRepresenting& request, uint32_t cur_epoch_num, MDB_txn* txn);
bool applyRequest(logos::block_store& store, StopRepresenting& request, uint32_t cur_epoch_num, MDB_txn* txn);

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
