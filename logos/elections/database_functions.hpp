#pragma once
#include <logos/blockstore.hpp>
#include <logos/elections/requests.hpp>
#include <logos/lib/epoch_time_util.hpp>
#include <unordered_set>

std::vector<std::pair<AccountAddress,CandidateInfo>> getElectionWinners(size_t num_winners, logos::block_store& store, MDB_txn* txn);



const std::chrono::milliseconds VOTING_DOWNTIME(1000 * 60 * 10); //10 minutes
const uint32_t START_ELECTIONS_EPOCH = 10; //Hold elections starting in epoch 10
const uint32_t TERM_LENGTH = 4; //Number of epochs to stay in office
const Amount MIN_REP_STAKE = 1;
const Amount MIN_DELEGATE_STAKE = 1;

class ElectionsConfig
{
    public:
    static uint32_t START_ELECTIONS_EPOCH;
    static uint32_t TERM_LENGTH;
};

bool getOldEpochBlock(
        logos::block_store& store,
        MDB_txn* txn,
        size_t num_epochs_ago,
        ApprovedEB& output);

bool shouldForceRetire(uint32_t next_epoch_number);
std::unordered_set<Delegate> getDelegatesToForceRetire(logos::block_store& store,
        uint32_t next_epoch_num, MDB_txn* txn);


//TODO: make some of these private
//want to be able to unit test each of them individually
//but dont want clients to call some of them
//TODO: maybe make these methods of a struct, where the store and txn are members
bool updateCandidatesDB(logos::block_store& store, MDB_txn* txn);
bool markDelegateElectsAsRemove(logos::block_store& store, MDB_txn* txn);
bool addReelectionCandidates(logos::block_store& store, MDB_txn* txn);
bool transitionCandidatesDBNextEpoch(logos::block_store& store, MDB_txn* txn,bool reelection=true);
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
