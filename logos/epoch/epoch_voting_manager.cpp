///
/// @file
/// This file contains definition of the EpochVotingManager class which handles epoch voting
///

#include <logos/node/delegate_identity_manager.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
#include <logos/node/node.hpp>
#include <logos/lib/trace.hpp>

#include <unordered_map>

void
EpochVotingManager::GetNextEpochDelegates(
   Delegates &delegates)
{
    int constexpr num_epochs = 4;
    int constexpr num_new_delegates = 8;
    ApprovedEB previous_epoch;
    BlockHash hash;
    std::unordered_map<AccountPubKey,bool> delegates_epochs;

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

    auto n_epochs = num_epochs - (IsFirstEpoch()?1:0);
    for (int e = 0; e < n_epochs; ++e)
    {
        if (_store.epoch_get(hash, previous_epoch))
        {
            LOG_FATAL(_log) << "EpochVotingManager::GetNextEpochDelegates failed to get epoch: "
                            << hash.to_string();
            trace_and_halt();
        }
        hash = previous_epoch.previous;
        for (int del = 0; del < NUM_DELEGATES; ++del)
        {
            Delegate &delegate = previous_epoch.delegates[del];
            delegates_epochs[delegate.account] = true;
            if (e == 0)
            {
                // populate new delegates from the most recent epoch
                delegates[del] = delegate;
            }
        }
    }

    // replace first 8
    int new_delegate = 0;
    for (auto delegate : logos::genesis_delegates)
    {
       if (delegates_epochs.find(delegate.key.pub) == delegates_epochs.end())
       {
           delegates[new_delegate].account = delegate.key.pub;
           {
               //delegates[new_delegate].bls_pub = delegate.bls_key.pub;
               //TODO simplify bls serialize functions
               std::string s;
               delegate.bls_key.pub.serialize(s);
               assert(s.size() == CONSENSUS_PUB_KEY_SIZE);
               memcpy(delegates[new_delegate].bls_pub.data(), s.data(), CONSENSUS_PUB_KEY_SIZE);
           }
           delegates[new_delegate].stake = delegate.stake;
           delegates[new_delegate].vote = delegate.vote;
          ++new_delegate;
          if (num_new_delegates == new_delegate)
          {
              break;
          }
       }
    }
    std::sort(std::begin(delegates), std::end(delegates),
        [](const Delegate &d1, const Delegate &d2){return d1.stake < d2.stake;});
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

bool
EpochVotingManager::IsFirstEpoch()
{
    BlockHash hash;
    ApprovedEB epoch;

    if (_store.epoch_tip_get(hash))
    {
        Log log;
        LOG_ERROR(log) << "Archiver::IsFirstEpoch failed to get epoch tip. Genesis blocks are being generated.";
        return true;
    }

    if (_store.epoch_get(hash, epoch))
    {
        LOG_ERROR(_log) << "Archiver::IsFirstEpoch failed to get epoch: "
                        << hash.to_string();
        return false;
    }

    return epoch.epoch_number == GENESIS_EPOCH;
}
