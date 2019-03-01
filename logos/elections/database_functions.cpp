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

std::vector<std::pair<AccountAddress,CandidateInfo>> getElectionWinners(
        size_t num_winners,
        logos::block_store& store,
        MDB_txn* txn)
{

    FixedSizeHeap<std::pair<AccountAddress,CandidateInfo>> results(num_winners,
            [](auto p1,auto p2)
            {
            //TODO: tiebreakers
                return p1.second.votes_received_weighted > p2.second.votes_received_weighted;
            });
    for(auto it = logos::store_iterator(txn, store.candidacy_db);
           it != logos::store_iterator(nullptr); ++it)
    {
        bool error = false;
        CandidateInfo candidate_info(error,it->second);
        if(!error)
        {
            auto pair = std::make_pair(it->first.uint256(),candidate_info);
            results.try_push(pair);
        } 
    }
    return results.getResults();
}

uint32_t ElectionsConfig::START_ELECTIONS_EPOCH = 10;
uint32_t ElectionsConfig::TERM_LENGTH = 4;

std::unordered_set<Delegate> getDelegatesToForceRetire(logos::block_store& store,
        uint32_t next_epoch_num,
        MDB_txn* txn)
{
    std::unordered_set<Delegate> to_retire;
    ApprovedEB epoch;
    size_t num_epochs_ago = next_epoch_num - ElectionsConfig::START_ELECTIONS_EPOCH - 1;
    assert(num_epochs_ago <= 4);
    getOldEpochBlock(store, txn, num_epochs_ago, epoch);
    size_t offset = num_epochs_ago * (NUM_DELEGATES / ElectionsConfig::TERM_LENGTH); 

    for(size_t i = offset; i < offset + (NUM_DELEGATES / ElectionsConfig::TERM_LENGTH); ++i)
    {
        to_retire.insert(epoch.delegates[i]);
    }
    return to_retire;
}

bool shouldForceRetire(uint32_t next_epoch_number)
{
    return next_epoch_number > ElectionsConfig::START_ELECTIONS_EPOCH 
        && next_epoch_number <= ElectionsConfig::START_ELECTIONS_EPOCH + ElectionsConfig::TERM_LENGTH;
}

bool markDelegateElectsAsRemove(logos::block_store& store, MDB_txn* txn)
{
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

bool getOldEpochBlock(
        logos::block_store& store,
        MDB_txn* txn,
        size_t num_epochs_ago,
        ApprovedEB& output)
{
    BlockHash hash;
    if(store.epoch_tip_get(hash,txn))
    {
        return true;
    }
    assert(!store.epoch_get(hash,output,txn));

    for(size_t i = 0; i < num_epochs_ago; ++i)
    {
        auto previous_hash = output.previous;
        if(previous_hash == 0)
        {
            //not enough epochs have passed
            return true;
        }
        //This should never happen
        assert(!store.epoch_get(previous_hash,output,txn));
    }
    return false;
}

bool addReelectionCandidates(logos::block_store& store, MDB_txn* txn)
{
    ApprovedEB epoch;
    if(getOldEpochBlock(store,txn,3,epoch))
    {
        return true;
    }

    for(auto& d : epoch.delegates)
    {
        if(d.starting_term)
        {
            store.candidate_add_new(d.account,d.bls_pub,d.stake,txn);
        }
    }
    return false;
}

bool updateCandidatesDB(logos::block_store& store, MDB_txn* txn)
{
    bool res = false;
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


bool transitionCandidatesDBNextEpoch(logos::block_store& store, MDB_txn* txn,bool reelection)
{
    bool result = false;
    if(reelection && addReelectionCandidates(store, txn))
    {
        std::cout << "failed to add reelection candidates" << std::endl;
        result = true;
    }
    if(markDelegateElectsAsRemove(store, txn))
    {
        std::cout << "failed to mark delegate elects as remove" << std::endl;
        result = true;
    }
    if(updateCandidatesDB(store, txn))
    {
        std::cout << "failed to update candidates db" << std::endl;
        result = true;
    }
    return result;
}

bool isValid(logos::block_store& store, const ElectionVote& vote_request, uint32_t cur_epoch_num, MDB_txn* txn, logos::process_return & result)
{
    std::cout << "epoch boundary" << std::endl;
    if(!isOutsideOfEpochBoundary(store,cur_epoch_num,txn))
    {
        result.code = logos::process_result::dead_period_vote;
        return false;
    }
    RepInfo info;
    //are you a rep at all?
    std::cout << "rep get" << std::endl;
    if(store.rep_get(vote_request.origin,info,txn))
    {
        result.code = logos::process_result::not_a_rep;
        return false;
    }

    //What is your status as a rep
    if(!info.announced_stop && info.rep_action_epoch == cur_epoch_num)
    {
        result.code = logos::process_result::pending_rep;
        return false;
    }
    else if(info.announced_stop && info.rep_action_epoch != cur_epoch_num)
    {
        result.code = logos::process_result::old_rep;
        return false;
    }

    //did you vote already this epoch?
    if(info.last_epoch_voted == cur_epoch_num)
    {
        result.code = logos::process_result::already_voted;
        return false;
    }

    size_t total = 0;
    //are these proper votes?

    for(auto& cp : vote_request.votes_)
    {
        total += cp.num_votes;
        CandidateInfo info;
        if(store.candidate_get(cp.account,info,txn) || !info.active)
        {
            result.code = logos::process_result::invalid_candidate;
            return false;
        }
    }
    return total <= MAX_VOTES;
}

bool isValid(
        logos::block_store& store,
        AnnounceCandidacy& request,
        uint32_t cur_epoch_num,
        MDB_txn* txn)
{
    if(!isOutsideOfEpochBoundary(store,cur_epoch_num,txn))
    {
        return false;
    }
    RepInfo r_info;
    //TODO: do we really need this requirement, that you are a rep to be a candidate
    if(store.rep_get(request.origin,r_info,txn))
    {
        return false;
    }
    Amount stake = request.stake != 0 ? request.stake : r_info.stake;
    if(stake < MIN_DELEGATE_STAKE)
    {
        return false;
    }
    if(!r_info.announced_stop && r_info.rep_action_epoch==cur_epoch_num)
    {
        return false;
    }
    CandidateInfo c_info;
    return store.candidate_get(request.origin,c_info,txn);
}

bool isValid(
        logos::block_store& store,
        RenounceCandidacy& request,
        uint32_t cur_epoch_num,
        MDB_txn* txn)
{
    if(!isOutsideOfEpochBoundary(store,cur_epoch_num,txn))
    {
        return false;
    }
    CandidateInfo info;
    if(store.candidate_get(request.origin,info,txn))
    {
        return false;
    }
    return info.active;
}

//TODO how to make this unit testable? We might end up too close to the epoch boundary
//during a unit test
bool isOutsideOfEpochBoundary(logos::block_store& store, uint32_t cur_epoch_num, MDB_txn* txn)
{
//    EpochTimeUtil util;
//    auto lapse = util.GetNextEpochTime(false);
//    if(lapse < VOTING_DOWNTIME)
//    {
//        return false;
//    }


    BlockHash hash; 
    //has the epoch block been created?
    if(store.epoch_tip_get(hash,txn))
    {
        Log log;
        LOG_INFO(log) << "couldn't get epoch tip";
        return false;
    }
    
    ApprovedEB eb;
    if(store.epoch_get(hash,eb,txn))
    {
        Log log;
        LOG_INFO(log) << "couldn't get epoch";
        return false;
    }

    Log log;
    LOG_INFO(log) << "get epoch and epoch tip";
    return eb.epoch_number == cur_epoch_num;
}

bool applyRequest(logos::block_store& store, const ElectionVote& request, uint32_t cur_epoch_num, MDB_txn* txn)
{
    RepInfo rep;
    if(store.rep_get(request.origin,rep,txn))
    {
        return false;
    }
    rep.last_epoch_voted = cur_epoch_num;
    rep.election_vote_tip = request.Hash();
    if(store.rep_put(request.origin,rep,txn))
    {
        return false;
    }
    if(store.request_put(request,txn))
    {
        mdb_txn_abort(txn);
        return false;
    }
    for(auto& p : request.votes_)
    {
        if(store.candidate_add_vote(p.account,p.num_votes*rep.stake.number(),txn))
        {
            mdb_txn_abort(txn);
            return false;
        }
    }
    return true;
}
//TODO: this doesn't store the request itself
bool applyRequest(logos::block_store& store, AnnounceCandidacy& request, MDB_txn* txn)
{
    auto stake = request.stake;
    RepInfo rep;
    if(store.rep_get(request.origin,rep, txn))
    {
        return true;
    }
    if(request.stake > 0)
    {

        rep.stake = request.stake;
        if(store.rep_put(request.origin,rep,txn))
        {
            return true;
        }
    }
    else
    {
        stake = rep.stake;
    }


    return !store.candidate_add_new(request.origin,request.bls_key,stake,txn);
}

bool applyRequest(logos::block_store& store, RenounceCandidacy& request, MDB_txn* txn)
{
    return !store.candidate_mark_remove(request.origin,txn);
}

bool isValid(logos::block_store& store, StartRepresenting& request, uint32_t cur_epoch_num, MDB_txn* txn)
{
    if(!isOutsideOfEpochBoundary(store,cur_epoch_num,txn))
    {
        return false;
    }

    if(request.stake < MIN_REP_STAKE)
    {
        return false;
    } 

    RepInfo rep;
    if(!store.rep_get(request.origin,rep,txn))
    {
        //Only accept if the rep is supposed to be removed but hasnt
        return rep.announced_stop && rep.rep_action_epoch != cur_epoch_num;   
    }
    return true;
}

bool isValid(logos::block_store& store, StopRepresenting& request, uint32_t cur_epoch_num, MDB_txn* txn)
{
    if(!isOutsideOfEpochBoundary(store,cur_epoch_num,txn))
    {
        return false;
    }

    CandidateInfo candidate;
    if(!store.candidate_get(request.origin,candidate,txn))
    {
        //cant stop representing if you are a candidate
        return false;
    }

    ApprovedEB epoch;
    BlockHash hash;
    assert(!store.epoch_tip_get(hash,txn));
    assert(!store.epoch_get(hash,epoch,txn));

    for(size_t i = 0; i < NUM_DELEGATES; ++i)
    {
        if(epoch.delegates[i].account == request.origin)
        {
            //can't stop representing if you are a delegate or delegate elect
            return false;
        }
    }

    RepInfo rep;
    if(!store.rep_get(request.origin,rep,txn))
    {
        return !rep.announced_stop && rep.rep_action_epoch != cur_epoch_num;
    }
    return false;
}

bool applyRequest(logos::block_store& store, StartRepresenting& request, uint32_t cur_epoch_num, MDB_txn* txn)
{
    RepInfo rep;
    rep.rep_action_epoch = cur_epoch_num;
    rep.announced_stop = false;
    rep.stake = request.stake;
    return !store.rep_put(request.origin,rep,txn);
}

bool applyRequest(logos::block_store& store, StopRepresenting& request, uint32_t cur_epoch_num, MDB_txn* txn)
{
    RepInfo rep;
    if(store.rep_get(request.origin,rep,txn))
    {
        return false;
    }
    rep.announced_stop = true;
    rep.rep_action_epoch = cur_epoch_num;
    return !store.rep_put(request.origin,rep,txn);
}

template class FixedSizeHeap<int>;

