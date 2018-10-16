///
/// @file
/// This file contains definition of the EpochVotingManager class which handles epoch voting
///

#include "epoch_voting_manager.hpp"
#include <logos/node/node.hpp>

#include <unordered_map>

void
EpochVotingManager::GetNextEpochDelegates(
   Delegates &delegates)
{
    int constexpr num_epochs = 3;
    int constexpr num_new_delegates = 8;
    Epoch previous_epoch;
    BlockHash hash;
    std::unordered_map<logos::public_key,bool> delegates3epochs;

    // get all delegate in the previous 3 epochs
    if (_store.epoch_tip_get(hash))
    {
        BOOST_LOG(_log) << "EpochVotingManager::GetNextEpochDelegates failed to get epoch tip";
        return;
    }

    for (int e = 0; e < num_epochs; ++e)
    {
        if (_store.epoch_get(hash, previous_epoch))
        {
            BOOST_LOG(_log) << "EpochVotingManager::GetNextEpochDelegates failed to get epoch: "
                            << hash.to_string();
            return;
        }
        hash = previous_epoch.previous;
        for (int del = 0; del < NUM_DELEGATES; ++del)
        {
            Delegate &delegate = previous_epoch._delegates[del];
            delegates3epochs[delegate._account] = true;
            if (e == 0)
            {
                // populate new delegates from the most recent epoch
                delegates[del] = delegate;
            }
        }
    }

    // replace last 8 for now
    int new_delegate = NUM_DELEGATES - num_new_delegates;
    for (auto delegate : logos::genesis_delegates)
    {
       if (delegates3epochs.find(delegate.key.pub) == delegates3epochs.end())
       {
          delegates[new_delegate]._account = delegate.key.pub;
           delegates[new_delegate]._stake = delegate._stake;
           delegates[new_delegate]._vote = delegate._vote;
          ++new_delegate;
          if (NUM_DELEGATES == new_delegate)
          {
              break;
          }
       }
    }
    std::sort(std::begin(delegates), std::end(delegates),
        [](const Delegate &d1, const Delegate &d2){return d1._stake < d2._stake;});
}

bool
EpochVotingManager::ValidateEpochDelegates(
   const Delegates &delegates)
{
   std::unordered_map<logos::public_key,bool> verify;

   for (auto delegate : logos::genesis_delegates)
   {
       verify[delegate.key.pub] = true;
   }

   for (int i = 0; i < NUM_DELEGATES; ++i)
   {
       if (verify.find(delegates[i]._account) == verify.end())
       {
           BOOST_LOG(_log) << "EpochVotingManager::ValidateEpochDelegates invalild account "
                           << delegates[i]._account.to_account();
           return false;
       }
   }

   return true;
}
