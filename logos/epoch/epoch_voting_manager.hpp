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
#include <logos/elections/candidate.hpp>
#include <logos/elections/representative.hpp>


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
    void GetNextEpochDelegates(Delegates& delegates, uint32_t next_epoch_num);

    /// Verify epoch delegates
    /// @param delegates list of epoch delegates [in,out]
    /// @returns true if valid
    bool ValidateEpochDelegates(
            const Delegates &delegates,
            uint32_t next_epoch_num);

    /// Get the set of delegates in their last epoch
    /// @returns set of delegates in their last epoch
    std::unordered_set<Delegate> GetRetiringDelegates(uint32_t next_epoch_num);

    /// Get the list of delegates that will take office next epoch
    /// @param num_new number of new delegates to get
    /// @returns vector of delegate-elects
    std::vector<Delegate> GetDelegateElects(size_t num_new, uint32_t next_epoch_num);

    void RedistributeVotes(Delegates &delegates);

    bool ShouldForceRetire(uint32_t next_epoch_num);

    std::unordered_set<Delegate> GetDelegatesToForceRetire(uint32_t next_epoch_num);

    static bool IsGreater(const Delegate& d1, const Delegate& d2);

    std::vector<std::pair<AccountAddress,CandidateInfo>> 
    GetElectionWinners(size_t num_winners);
    static uint32_t START_ELECTIONS_EPOCH;
    static uint32_t TERM_LENGTH;


private:

    BlockStore &    _store; ///< logos block store reference
    Log             _log;   ///< boost asio log

};

