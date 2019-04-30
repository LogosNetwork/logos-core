///
/// @file
/// This file contains definition of the EpochVotingManager class which handles epoch voting
///

#include <logos/identity_management/delegate_identity_manager.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
#include <logos/elections/requests.hpp>
#include <logos/node/node.hpp>
#include <logos/lib/trace.hpp>
#include <logos/elections/candidate.hpp>
#include <logos/elections/representative.hpp>
#include <unordered_map>
#include <logos/consensus/consensus_container.hpp>

std::vector<std::pair<AccountAddress,CandidateInfo>>
EpochVotingManager::GetElectionWinners(
        size_t num_winners)
{

    logos::transaction txn(_store.environment,nullptr,false);

    std::vector<std::pair<AccountAddress, CandidateInfo>> winners;

    for(auto it = logos::store_iterator(txn, _store.leading_candidates_db);
           it != logos::store_iterator(nullptr); ++it)
    {
        bool error = false;
        CandidateInfo candidate_info(error,it->second);
        if(!error)
        {
            auto pair = std::make_pair(it->first.uint256(),candidate_info);
            winners.push_back(pair);
        }
        else
        {
            assert(false);
        }
    }

    return winners;
}

uint32_t EpochVotingManager::START_ELECTIONS_EPOCH = 50;
uint32_t EpochVotingManager::TERM_LENGTH = 4;
bool EpochVotingManager::ENABLE_ELECTIONS = false;

//these are the delegates that are in their last epoch
std::unordered_set<Delegate> EpochVotingManager::GetRetiringDelegates(
        uint32_t next_epoch_num)
{
    std::unordered_set<Delegate> retiring;

    bool do_verify = false;
    if(ShouldForceRetire(next_epoch_num))
    {
        retiring = GetDelegatesToForceRetire(next_epoch_num);
        do_verify = true;
    }
    else if(next_epoch_num > START_ELECTIONS_EPOCH)
    {
        ApprovedEB epoch;
        auto is_not_extension = [](ApprovedEB& eb)
        {
            return !eb.is_extension;
        };
        _store.epoch_get_n(3,epoch,nullptr,is_not_extension);

        for(auto& d : epoch.delegates)
        {
            if(d.starting_term)
            {
                d.starting_term = false;
                retiring.insert(d);
            }
        }
        do_verify = true;
    }

    if(do_verify)
    {
        ApprovedEB epoch;
        _store.epoch_get_n(0, epoch);
        size_t num_found = 0;
        for(auto& d : epoch.delegates)
        {
            if(retiring.find(d) != retiring.end())
            {
                ++num_found;
            }
        }
        assert(num_found == 8);
    }

    return retiring;
}

std::unordered_set<Delegate>
EpochVotingManager::GetDelegatesToForceRetire(uint32_t next_epoch_num)
{
    std::unordered_set<Delegate> to_retire;
    size_t num_epochs_ago = next_epoch_num - START_ELECTIONS_EPOCH - 1;
    //TODO < not <=
    assert(num_epochs_ago < 4);
    ApprovedEB epoch;
    _store.epoch_get_n(num_epochs_ago, epoch);
    size_t offset = num_epochs_ago * (NUM_DELEGATES / TERM_LENGTH);

    for(size_t i = offset; i < offset + (NUM_DELEGATES / TERM_LENGTH); ++i)
    {
        to_retire.insert(epoch.delegates[i]);
    }
    return to_retire;
}

bool EpochVotingManager::ShouldForceRetire(uint32_t next_epoch_number)
{
    return next_epoch_number > START_ELECTIONS_EPOCH
        && next_epoch_number <= START_ELECTIONS_EPOCH + TERM_LENGTH;
}

//these are the delegate-elects
std::vector<Delegate> EpochVotingManager::GetDelegateElects(size_t num_new, uint32_t next_epoch_num)
{

    if(next_epoch_num > START_ELECTIONS_EPOCH)
    {
        auto results = GetElectionWinners(num_new);
        std::vector<Delegate> delegate_elects(results.size());
        std::transform(results.begin(),results.end(),delegate_elects.begin(),
                [this](auto p){

                    Delegate d(
                            p.first,
                            p.second.bls_key,
                            p.second.ecies_key,
                            p.second.votes_received_weighted,
                            p.second.stake);
                    d.starting_term = true;
                    return d;
                });
        return delegate_elects;
    }
    else
    {
        return {};
    }
}

bool EpochVotingManager::GetNextEpochDelegates(
        Delegates& delegates,
        uint32_t next_epoch_num)
{
    int num_new_delegates = next_epoch_num > START_ELECTIONS_EPOCH && ENABLE_ELECTIONS 
        ? NUM_DELEGATES / TERM_LENGTH : 0;

    ApprovedEB previous_epoch;
    BlockHash hash;
    Tip tip;
    if (_store.epoch_tip_get(tip))
    {
        LOG_FATAL(_log) << "EpochVotingManager::GetNextEpochDelegates failed to get epoch tip";
        trace_and_halt();
    }
    hash = tip.digest;

    if (!DelegateIdentityManager::IsEpochTransitionEnabled() || !ENABLE_ELECTIONS)
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
        return true;
    }

    if (_store.epoch_get(hash, previous_epoch))
    {
        LOG_FATAL(_log) << "EpochVotingManager::GetNextEpochDelegates failed to get epoch: "
            << hash.to_string();
        trace_and_halt();
    }

    std::unordered_set<Delegate> retiring_dels = GetRetiringDelegates(next_epoch_num);
    std::unordered_set<AccountAddress> retiring;
    std::for_each(
            retiring_dels.begin(),
            retiring_dels.end(),
            [&retiring](const Delegate& del) { 
            retiring.insert(del.account);}
            );
    std::vector<Delegate> delegate_elects = GetDelegateElects(num_new_delegates, next_epoch_num);
    bool extend = false;
    if(delegate_elects.size() != num_new_delegates)
    {
        LOG_ERROR(_log) << "EpochVotingManager::GetNextEpochDelegates not enough delegate elects. "
            << "Extending term of retiring delegates by one epoch";
        extend = true;
    }
    else if(retiring.size() != num_new_delegates)
    {
        LOG_FATAL(_log) << "EpochVotingManager::GetNextEpochDelegates mismatch"
           << " in size of retiring and delegate_elects. Need to be equal."
           << "Delegate-elects size : " << delegate_elects.size()
            << " . Retiring size " << retiring.size();
        trace_and_halt();
    }

    size_t del_elects_idx = 0;
    for (int del = 0; del < NUM_DELEGATES; ++del)
    {
        if(retiring.find(previous_epoch.delegates[del].account)!=retiring.end())
        {
            //if we need to extend the current delegate set,
            //but we are in the period where we are force retiring
            //genesis delegates, simply act like the genesis delgates
            //to be retired were reelected. This will extend the
            //genesis delegate term by 4 more epochs. If extending term
            //of non-genesis delegate, term is extended by 1 epoch
            //This is done to keep the logic for force retiring genesis
            //delegates simple
            if(extend)
            {
                delegates[del] = previous_epoch.delegates[del];
                if(ShouldForceRetire(next_epoch_num))
                {
                    delegates[del].starting_term = true;
                }
                else
                {
                    delegates[del].starting_term = false;
                }
            }
            else
            {
                delegates[del] = delegate_elects[del_elects_idx];
                ++del_elects_idx;
            }
        } else
        {
            delegates[del] = previous_epoch.delegates[del];
            delegates[del].starting_term = false;
        }
    }

    if(!extend)
    {
        assert(del_elects_idx == delegate_elects.size());
    }

    std::sort(std::begin(delegates), std::end(delegates),
            EpochVotingManager::IsGreater
            );
    RedistributeVotes(delegates);
    //don't mark this epoch block as extended if extending genesis delegates terms
    if(extend)
    {
        return ShouldForceRetire(next_epoch_num);
    }
    return true;
}

bool EpochVotingManager::IsGreater(const Delegate& d1, const Delegate& d2)
{
    if(d1.vote != d2.vote)
    {
        return d1.vote > d2.vote;
    } else if(d1.stake != d2.stake)
    {
        return d1.stake > d2.stake;
    } else
    {
        return Blake2bHash(d1).number() > Blake2bHash(d2).number();
    }
}

void EpochVotingManager::RedistributeVotes(Delegates &delegates)
{
    Amount total_votes = 0;

    for(int del = 0; del < NUM_DELEGATES; ++del)
    {
        if(delegates[del].vote == 0)
        {

            delegates[del].vote = 1;
        }
        total_votes += delegates[del].vote;
    }

    Amount cap = total_votes.number() / 8;

    for(int del = 0; del < NUM_DELEGATES; ++del)
    {
        if(delegates[del].vote > cap)
        {
            total_votes -= delegates[del].vote;
            Amount rem = delegates[del].vote - cap;
            delegates[del].vote = cap;
            Amount add_back = 0;
            for(int i = del + 1; i < NUM_DELEGATES; ++i)
            {

                /*
                 * This operation can cause us to lose votes because of integer
                 * rounding. This is not ideal, but overall, the loss of votes
                 * is tolerated, since we will lose less than one vote each time
                 * this line is ran, which is at most 31 times for a given
                 * delegate. These votes are already weighted by the stake
                 * of the representative who cast them, which means the votes
                 * received should be much greater than 31. Therefore, a
                 * delegate losing 31 votes is somewhat negligible. The only time
                 * this is really a problem is when a delegate has received 0
                 * votes; in this situation, whether or not a delegate receives
                 * 31 additional votes does make a large difference. However,
                 * if a delegate received 0 votes, nobody voted for them at all
                 * and we are doing them a favor by giving them any amount of
                 * votes for free
                 */
                Amount to_add = ((delegates[i].vote.number() * rem.number())
                        / total_votes.number());

                delegates[i].vote += to_add;
                add_back += to_add;
            }
            total_votes += add_back;
        }
    }
}



bool
EpochVotingManager::ValidateEpochDelegates(
   const Delegates &delegates,
   uint32_t next_epoch_num)
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

   Delegates computed_delegates;
   GetNextEpochDelegates(computed_delegates, next_epoch_num);

   for(size_t i = 0; i < NUM_DELEGATES; ++i)
   {
        if(computed_delegates[i] != delegates[i])
        {
            return false;
        }
   }


   return true;
}
