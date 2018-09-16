///
/// @file
/// This file contains definition of the EpochVotingManager class which handles epoch voting
///
#pragma once

#include <logos/epoch/epoch.hpp>

namespace logos
{
    class block_store;
}


/// Epoch voting manager
class EpochVotingManager {
    using BlockStore = logos::block_store;
public:
    /// Class constructor
    /// @param store logos block store reference [in]
    EpochVotingManager(BlockStore &store)
        : _store(store)
        {}

    ~EpochVotingManager() = default;

    /// Get the list of next epoch delegates
    /// @param delegates list of new delegates [in,out]
    void GetNextEpochDelegates(std::array<Delegate, NUM_DELEGATES> &delegates);

    /// Verify epoch delegates
    /// @param delegates list of epoch delegates [in,out]
    /// @returns true if valid
    bool ValidateEpochDelegates(const std::array<Delegate, NUM_DELEGATES> &delegates);

private:

    BlockStore &    _store; ///< logos block store reference
};

