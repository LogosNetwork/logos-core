#include <logos/elections/database_functions.hpp>
#include <logos/elections/database.hpp>
#include <logos/blockstore.hpp>
#include <logos/epoch/election_requests.hpp>
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

bool isValid(logos::block_store& store, ElectionVote& vote_request, uint32_t cur_epoch_num)
{
    if(!isOutsideOfEpochBoundary(store,cur_epoch_num))
    {
        return false;
    }
    logos::transaction txn(store.environment,nullptr,true);
    RepInfo info;
    //are you a rep at all?
    if(store.rep_get(vote_request.origin,info,txn))
    {
        return false;
    }

    //What is your status as a rep
    if(!info.announced_stop && info.rep_action_epoch == cur_epoch_num)
    {
        return false;
    }
    else if(info.rep_action_epoch != cur_epoch_num)
    {
        return false;
    }

    //did you vote already this epoch?
    if(info.last_epoch_voted == cur_epoch_num)
    {
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
            return false;
        }
    }
    return total <= MAX_VOTES;
}

bool isValid(
        logos::block_store& store,
        AnnounceCandidacy& request,
        uint32_t cur_epoch_num)
{
    if(!isOutsideOfEpochBoundary(store,cur_epoch_num))
    {
        return false;
    }
    logos::transaction txn(store.environment,nullptr,true);
    RepInfo r_info;
    //TODO: do we really need this requirement, that you are a rep to be a candidate
    if(store.rep_get(request.origin,r_info,txn))
    {
        return false;
    }
    CandidateInfo c_info;
    return store.candidate_get(request.origin,c_info,txn);
}

bool isValid(
        logos::block_store& store,
        RenounceCandidacy& request,
        uint32_t cur_epoch_num)
{
    if(!isOutsideOfEpochBoundary(store,cur_epoch_num))
    {
        return false;
    }
    logos::transaction txn(store.environment,nullptr,true);
    CandidateInfo info;
    if(store.candidate_get(request.origin,info,txn))
    {
        return false;
    }
    return info.active;
}

bool isOutsideOfEpochBoundary(logos::block_store& store, uint32_t cur_epoch_num)
{
    EpochTimeUtil util;
    auto lapse = util.GetNextEpochTime(false);
    if(lapse < VOTING_DOWNTIME)
    {
        return false;
    }

    logos::transaction txn(store.environment,nullptr,true);
    BlockHash hash; 
    //has the epoch block been created?
    if(store.epoch_tip_get(hash,txn))
    {
        return false;
    }
    
    ApprovedEB eb;
    if(store.epoch_get(hash,eb,txn))
    {
       if(eb.epoch_number != cur_epoch_num)
       {
            return false;
       } 
    }
    return true;
}

bool applyElectionVote(logos::block_store& store, ElectionVote& request, uint32_t cur_epoch_num)
{
    logos::transaction txn(store.environment,nullptr,true);
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
    if(store.request_put(request,request.Hash(),txn))
    {
        mdb_txn_abort(txn);
        return false;
    }
    for(auto& p : request.votes_)
    {
        if(store.candidate_add_vote(p.account,p.num_votes*rep.stake,txn))
        {
            mdb_txn_abort(txn);
            return false;
        }
    }
    return true;
}

bool applyCandidacyRequest(logos::block_store& store, AnnounceCandidacy& request)
{
    logos::transaction txn(store.environment,nullptr,true);
    return !store.candidate_add_new(request.origin,txn);
}

bool applyCandidacyRequest(logos::block_store& store, RenounceCandidacy& request)
{
    logos::transaction txn(store.environment,nullptr,true);
    return !store.candidate_mark_remove(request.origin,txn);
}

bool isValid(logos::block_store& store, StartRepresenting& request, uint32_t cur_epoch_num)
{
    if(!isOutsideOfEpochBoundary(store,cur_epoch_num))
    {
        return false;
    }

    logos::transaction txn(store.environment,nullptr,true);
    RepInfo rep;
    if(!store.rep_get(request.origin,rep,txn))
    {
        //Only accept if the rep is supposed to be removed but hasnt
        return rep.announced_stop && rep.rep_action_epoch != cur_epoch_num;   
    }
    return true;
}

bool isValid(logos::block_store& store, StopRepresenting& request, uint32_t cur_epoch_num)
{
    if(!isOutsideOfEpochBoundary(store,cur_epoch_num))
    {
        return false;
    }

    logos::transaction txn(store.environment,nullptr,true);
    RepInfo rep;
    if(!store.rep_get(request.origin,rep,txn))
    {
        return !rep.announced_stop && rep.rep_action_epoch;
    }
    return false;
}

bool applyRequest(logos::block_store& store, StartRepresenting& request, uint32_t cur_epoch_num)
{
    logos::transaction txn(store.environment,nullptr,true);
    RepInfo rep;
    rep.rep_action_epoch = cur_epoch_num;
    return !store.rep_put(request.origin,rep,txn);
}

bool applyRequest(logos::block_store& store, StopRepresenting& request, uint32_t cur_epoch_num)
{
    logos::transaction txn(store.environment,nullptr,true);
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

