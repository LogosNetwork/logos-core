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
    /// @param next_epoch_num the number of the next epoch
    /// @returns false if a term extension was needed (not enough elected delegates)
    bool GetNextEpochDelegates(Delegates& delegates, uint32_t next_epoch_num);

    /// Verify epoch delegates
    /// @param delegates list of epoch delegates [in,out]
    /// @param next_epoch_num the number of the next epoch
    /// @returns true if valid
    bool ValidateEpochDelegates(
            const Delegates &delegates,
            uint32_t next_epoch_num);

    /// Get the set of delegates in their last epoch
    /// @param next_epoch_num the number of the next epoch
    /// @returns set of delegates in their last epoch
    std::unordered_set<Delegate> GetRetiringDelegates(uint32_t next_epoch_num);

    /// Get the list of delegates that will take office next epoch
    /// @param num_new number of new delegates to get
    /// @param next_epoch_num the number of the next epoch
    /// @returns vector of delegate-elects
    std::vector<Delegate> GetDelegateElects(size_t num_new, uint32_t next_epoch_num);

    /// Redistribute voting power or stake amongst elected delegates
    /// @param delegates list of epoch delegates [in,out]
    /// @param member struct member to redistribute (vote or stake)
    void Redistribute(Delegates &delegates, Amount Delegate::*member);

    /// Return if we need to force delegates to retire
    /// @param next_epoch_num the number of the next epoch
    /// @returns false if we retire oldest 8 
    bool ShouldForceRetire(uint32_t next_epoch_num);

    /// Return the delegates we are forcing to retire
    /// @param next_epoch_num the number of the next epoch
    /// @returns set of delegates to force retire
    std::unordered_set<Delegate> GetDelegatesToForceRetire(uint32_t next_epoch_num);

    /// Used to determine who has won the election
    /// @params d1, d2 delegates to compare
    /// @returns true if d1 should take precedence over d2 in election results
    static bool IsGreater(const Delegate& d1, const Delegate& d2);

    /// Get the winners of the most recent election
    /// @params num_winners number of winners to get
    /// @returns list of winners and their election data
    std::vector<std::pair<AccountAddress,CandidateInfo>> 
    GetElectionWinners(size_t num_winners);

    static uint32_t START_ELECTIONS_EPOCH;
    static uint32_t TERM_LENGTH;
    static bool     ENABLE_ELECTIONS;

    /// Is this a first epoch after genesis
    /// @returns true if first epoch
    bool IsFirstEpoch();

private:

    BlockStore &    _store; ///< logos block store reference
    Log             _log;   ///< boost asio log

};

