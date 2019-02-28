///
/// @file
/// This file contains definition of the EpochVotingManager class which handles epoch voting
///

#include <logos/node/delegate_identity_manager.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
#include <logos/elections/requests.hpp>
#include <logos/node/node.hpp>
#include <logos/lib/trace.hpp>
#include <logos/elections/database.hpp>
#include <unordered_map>
#include <logos/elections/database_functions.hpp>


void EpochVotingManager::CacheElectionWinners(std::vector<std::pair<AccountAddress,CandidateInfo>>& winners)
{
    std::lock_guard<std::mutex> guard(_cache_mutex);
    _cached_election_winners = winners;
    _cache_written = true;
}


//these are the delegates that are in their last epoch
std::unordered_set<Delegate> EpochVotingManager::GetRetiringDelegates()
{
    std::unordered_set<Delegate> retiring;

    ApprovedEB epoch;
    logos::transaction txn(_store.environment,nullptr,false);
    if(getOldEpochBlock(_store, txn, 3, epoch))
    {
        LOG_INFO(_log) << "EpochVotingManager::GetRetiringDelegates "
            << "Not enough epochs have passed. Retiring 0 delegates";
        return retiring;
    }

    for(auto& d : epoch.delegates)
    {
        if(d.starting_term)
        {
            d.starting_term = false;
            retiring.insert(d);
        }
    }
    
    return retiring;
}

//these are the delegate-elects
std::vector<Delegate> EpochVotingManager::GetDelegateElects(size_t num_new)
{
    logos::transaction txn(_store.environment,nullptr,false);
    std::lock_guard<std::mutex> guard(_cache_mutex);
    auto results = _cached_election_winners;
    //TODO: this shouldnt assert, but should wait?
    assert(_cache_written);
    DelegatePubKey dummy_bls_pub;
    Amount dummy_stake = 1;
    std::vector<Delegate> delegate_elects(results.size());
    std::transform(results.begin(),results.end(),delegate_elects.begin(),
            [this](auto p){
                RepInfo info;
                _store.rep_get(p.first,info);

                Delegate d(p.first,p.second.bls_key,p.second.votes_received_weighted,info.stake);
                d.starting_term = true;
                return d;
            });
    return delegate_elects;
}

//TODO: does this really need to use an output variable? Just return the delegates
void
EpochVotingManager::GetNextEpochDelegates(
   Delegates &delegates, std::unordered_set<Delegate>* to_retire)
{
    int constexpr num_epochs = 3;
    int constexpr num_new_delegates = 8;
    ApprovedEB previous_epoch;
    BlockHash hash;
    std::unordered_map<AccountPubKey,bool> delegates3epochs;
    
    LOG_INFO(_log) << "EpochVotingManager::GetNextEpochDelegates";
    // get all delegate in the previous 3 epochs
    if (_store.epoch_tip_get(hash))
    {
        LOG_FATAL(_log) << "EpochVotingManager::GetNextEpochDelegates failed to get epoch tip";
        trace_and_halt();
    }

    if (!DelegateIdentityManager::IsEpochTransitionEnabled())
    {
        ApprovedEB epoch;
        if (_store.epoch_get(hash, epoch))
        {
            LOG_FATAL(_log) << "EpochVotingManager::GetNextEpochDelegates failed to get epoch: "
                << hash.to_string();
            trace_and_halt();
        }
        for (int del = 0; del < NUM_DELEGATES; ++del)
        {
            delegates[del] = epoch.delegates[del];
        }
        return;
    }

    if (_store.epoch_get(hash, previous_epoch))
    {
        LOG_FATAL(_log) << "EpochVotingManager::GetNextEpochDelegates failed to get epoch: "
            << hash.to_string();
        trace_and_halt();
    }

    std::unordered_set<Delegate> retiring = to_retire != nullptr ? *to_retire : GetRetiringDelegates();
    std::vector<Delegate> delegate_elects(GetDelegateElects(num_new_delegates));
    if(retiring.size() != delegate_elects.size())
    {
        LOG_FATAL(_log) << "EpochVotingManager::GetNextEpochDelegates mismatch"
           << " in size of retiring and delegate_elects. Need to be equal."
           << "Delegate-elects size : " << delegate_elects.size()
            << " . Retiring size " << retiring.size();
        trace_and_halt();
    }

    LOG_INFO(_log) << "EpochVotingManager::Delegate-Elects size is : " << delegate_elects.size()
        << " . Retiring size is " << retiring.size();

    size_t del_elects_idx = 0;
    for (int del = 0; del < NUM_DELEGATES; ++del)
    {
        if(retiring.find(previous_epoch.delegates[del])!=retiring.end())
        {
            delegates[del] = delegate_elects[del_elects_idx];
            ++del_elects_idx;
        } else
        {
            delegates[del] = previous_epoch.delegates[del];
            delegates[del].starting_term = false;
        }
    }

    std::sort(std::begin(delegates), std::end(delegates),
            [](const Delegate &d1, const Delegate &d2){
            return d1.vote < d2.vote;});
}

bool
EpochVotingManager::ValidateEpochDelegates(
   const Delegates &delegates)
{
   std::unordered_map<AccountPubKey, bool> verify;
   Log log;

   for (auto delegate : logos::genesis_delegates)
   {
       verify[delegate.key.pub] = true;
   }

   for (int i = 0; i < NUM_DELEGATES; ++i)
   {
       if (verify.find(delegates[i].account) == verify.end())
       {
           LOG_ERROR(log) << "EpochVotingManager::ValidateEpochDelegates invalild account "
                           << delegates[i].account.to_account();
           return false;
       }
   }

   return true;
}
