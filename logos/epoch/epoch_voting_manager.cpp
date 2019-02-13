///
/// @file
/// This file contains definition of the EpochVotingManager class which handles epoch voting
///

#include <logos/node/delegate_identity_manager.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
#include <logos/epoch/election_requests.hpp>
#include <logos/node/node.hpp>
#include <logos/lib/trace.hpp>
#include <logos/elections/database.hpp>
#include <unordered_map>



//these are the delegates that are in their last epoch
std::unordered_set<Delegate> EpochVotingManager::GetRetiringDelegates()
{
    std::unordered_set<Delegate> retiring;
    return retiring;
}

//these are the delegate-elects
std::vector<Delegate> EpochVotingManager::GetDelegateElects(size_t num_new)
{
    std::unordered_map<AccountAddress,uint128_t> results;
    logos::transaction txn(_store.environment, nullptr, false);
    for(auto it = logos::store_iterator(txn, _store.representative_db);
            it != logos::store_iterator(nullptr); ++it)
    {
        logos::account account(it->first.uint256());
        bool error = false;
        RepInfo rep_info(error, it->second);
        ElectionVote ev;
        if(!_store.request_get(rep_info.election_vote_tip,ev,txn))
        {
            //TODO: check if ev is for proper epoch by looking at batchhash
            for(const auto & cp: ev.votes_)
            {
                //TODO: weight the vote by stake
                results[cp.account] += cp.num_votes;
            }
        }
    }

    std::vector<std::pair<AccountAddress,uint128_t>> sorted(
            results.begin(), results.end());
    std::sort(sorted.begin(),sorted.end(),
            [](auto p1, auto p2){
                return p1.second > p2.second;
            }); //TODO: tiebreaker rules
    DelegatePubKey dummy_bls_pub;
    Amount dummy_stake = 1;
    std::vector<Delegate> all;
    all.resize(sorted.size());
    std::transform(sorted.begin(),sorted.end(),all.begin(),
            [dummy_bls_pub,dummy_stake](auto p){
                return Delegate(p.first,dummy_bls_pub,p.second,dummy_stake);
            });
    std::vector<Delegate> delegate_elects(all.begin(),all.begin()+num_new);
    return delegate_elects;
}

//TODO: does this really need to use an output variable? Just return the delegates
void
EpochVotingManager::GetNextEpochDelegates(
   Delegates &delegates)
{
    int constexpr num_epochs = 3;
    int constexpr num_new_delegates = 8;
    ApprovedEB previous_epoch;
    BlockHash hash;
    std::unordered_map<AccountPubKey,bool> delegates3epochs;

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

    std::unordered_set<Delegate> retiring(GetRetiringDelegates());
    std::vector<Delegate> delegate_elects(GetDelegateElects(num_new_delegates));
    if(retiring.size() != delegate_elects.size())
    {
        LOG_FATAL(_log) << "EpochVotingManager::GetNextEpochDelegates mismatch"
           << " in size of retiring and delegate_elects. Need to be equal"; 
        trace_and_halt();
    }

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
        }
    }

    std::sort(std::begin(delegates), std::end(delegates),
            [](const Delegate &d1, const Delegate &d2){
            return d1.stake < d2.stake;});
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
