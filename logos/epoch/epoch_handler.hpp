/// @file
/// This file contains declaration of the EpochHandler class, which is used
/// in the Epoch processing
#pragma once

#include <logos/epoch/epoch_voting_manager.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>
#include <logos/epoch/epoch.hpp>

namespace logos {
    class block_store;
    class transaction;
}

using BlockStore = logos::block_store;

// This value indicates the factor by which
// the total supply of native Logos currency
// increases with each passing epoch.
static const double LOGOS_INFLATION_RATE = 1.000035;

/// EpochHandler builds Epoch block
class EpochHandler
{
public:
    /// Class constructor
    /// @param store logos::block_store reference [in]
    /// @param voting_manager delegate's voting manager [in]
    EpochHandler(BlockStore &store,
                 EpochVotingManager & voting_manager)
        : _voting_manager(voting_manager)
        , _store(store)
    {}

    virtual ~EpochHandler() {}

    /// Build Epoch block
    /// @param block build the block [in|out]
    bool Build(DelegateMessage<ConsensusType::Epoch> &);

    static uint64_t ComputeNumRBs(BlockStore &store, uint32_t epoch_number);

private:

    EpochVotingManager & _voting_manager;  ///< voting manager
    BlockStore &         _store;
    Log                  _log;
};
