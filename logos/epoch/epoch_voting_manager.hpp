///
/// @file
/// This file contains definition of the EpochVotingManager class which handles epoch voting
///
#pragma once

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>
#include <logos/epoch/epoch.hpp>
#include <logos/lib/log.hpp>

#include <unordered_set>
#include <logos/elections/database.hpp>

namespace logos
{
    class block_store;
}


/// Epoch voting manager
class EpochVotingManager {
    using BlockStore = logos::block_store;
    using Delegates  = Delegate[NUM_DELEGATES];
public:
    /// Class constructor
    /// @param store logos block store reference [in]
    EpochVotingManager(BlockStore &store)
        : _store(store)
        {}

    ~EpochVotingManager() = default;

    /// Get the list of next epoch delegates
    /// @param delegates list of new delegates [in,out]
    void GetNextEpochDelegates(Delegates &delegates,std::unordered_set<Delegate>* to_retire = nullptr);

    /// Verify epoch delegates
    /// @param delegates list of epoch delegates [in,out]
    /// @returns true if valid
    static bool ValidateEpochDelegates(const Delegates &delegates);

    /// Get the set of delegates in their last epoch
    /// @returns set of delegates in their last epoch
    std::unordered_set<Delegate> GetRetiringDelegates();

    /// Get the list of delegates that will take office next epoch
    /// @param num_new number of new delegates to get
    /// @returns vector of delegate-elects
    std::vector<Delegate> GetDelegateElects(size_t num_new);

    //TODO: documentation
    void CacheElectionWinners(
            std::vector<std::pair<AccountAddress,CandidateInfo>>& winners);

    void InvalidateCache();


private:

    std::vector<std::pair<AccountAddress,CandidateInfo>> _cached_election_winners;
    std::mutex _cache_mutex; 
    bool _cache_written = false;
    BlockStore &    _store; ///< logos block store reference
    Log             _log;   ///< boost asio log

};

