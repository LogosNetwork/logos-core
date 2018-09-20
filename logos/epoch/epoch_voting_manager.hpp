///
/// @file
/// This file contains definition of the EpochVotingManager class which handles epoch voting
///
#pragma once

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>
#include <logos/epoch/epoch.hpp>

namespace logos
{
    class block_store;
}


/// Epoch voting manager
class EpochVotingManager {
    using BlockStore = logos::block_store;
    using Delegates  = Delegate[NUM_DELEGATES];
    using Log        = boost::log::sources::logger_mt;
public:
    /// Class constructor
    /// @param store logos block store reference [in]
    EpochVotingManager(BlockStore &store)
        : _store(store)
        {}

    ~EpochVotingManager() = default;

    /// Get the list of next epoch delegates
    /// @param delegates list of new delegates [in,out]
    void GetNextEpochDelegates(Delegates &delegates);

    /// Verify epoch delegates
    /// @param delegates list of epoch delegates [in,out]
    /// @returns true if valid
    bool ValidateEpochDelegates(const Delegates &delegates);

private:

    BlockStore &    _store; ///< logos block store reference
    Log             _log;   ///< boost asio log
};

