/// @file
/// This file contains declaration of the EpochHandler class, which is used
/// in the Epoch processing
#pragma once

#include <logos/consensus/persistence/epoch/epoch_persistence.hpp>
#include <logos/consensus/persistence/reservations.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>
#include <logos/epoch/epoch.hpp>

namespace logos {
    class block_store;
    class transaction;
}

using BlockStore = logos::block_store;

/// EpochHandler builds, validates, persists, triggers the Epoch
class EpochHandler : public PersistenceManager<ECT>
{
public:
    /// Class constructor
    /// @param store logos::block_store reference [in]
    /// @param voting_manager delegate's voting manager [in]
    EpochHandler(BlockStore &store,
                 EpochVotingManager & voting_manager)
        : PersistenceManager<ECT>(store)
        , _voting_manager(voting_manager)
        {}
    virtual ~EpochHandler() {}

    /// Build Epoch block
    /// @param block build the block [in|out]
    bool Build(Epoch&);

private:
    EpochVotingManager &    _voting_manager;  ///< voting manager
};