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
    std::array<Epoch, num_epochs> previous_epochs;
    BlockHash hash;
    std::unordered_map<logos::public_key,bool> all_delegates;

    // get all delegate in the previous 3 epochs
    assert(false == _store.epoch_tip_get(hash));
    for (int e = 0; e < num_epochs; ++e)
    {
        assert(false == _store.epoch_get(hash, previous_epochs[e]));
        hash = previous_epochs[e].previous;
        for (int i = 0, d = 0; i < NUM_DELEGATES; ++i)
        {
            Delegate &delegate = previous_epochs[e]._delegates[i];
            all_delegates[delegate._account] = true;
            if (e == (num_epochs-1))
            {
                // populate new delegates from the most recent epoch
                delegates[d] = delegate;
                ++d;
            }
        }
    }

    // replace first 8 for now
    int new_delegate = 0;
    for (auto delegate : logos::genesis_delegates)
    {
       if (all_delegates.find(delegate.key.pub) == all_delegates.end())
       {
          delegates[new_delegate]._account = delegate.key.pub;
           delegates[new_delegate]._stake = delegate._stake;
           delegates[new_delegate]._vote = delegate._vote;
          ++new_delegate;
          if (num_new_delegates == new_delegate)
          {
              break;
          }
       }
    }
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
           return false;
       }
   }

   return true;
}
