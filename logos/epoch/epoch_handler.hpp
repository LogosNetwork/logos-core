/// @file
/// This file contains declaration of the EpochHandler class, which is used
/// in the Epoch processing
#pragma once

#include <logos/consensus/persistence/persistence_manager.hpp>
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
class EpochHandler : public PersistenceManager<ConsensusType::Epoch>
{
public:
    /// Class constructor
    /// @param store logos::block_store reference [in]
    /// @param voting_manager delegate's voting manager [in]
    EpochHandler(BlockStore &store,
                 EpochVotingManager & voting_manager)
        : PersistenceManager<ConsensusType::Epoch>(store, _reservations)//_store(s)
        , _voting_manager(voting_manager)
        , _reservations(store)
        {}
    virtual ~EpochHandler() {}

    /// Validate Epoch block
    /// @param block block to validate [in]
    //bool Validate(const Epoch&);

    /// Persist Epoch block
    /// @param block save the block to the database [in]
    //void ApplyUpdates(const Epoch&);

    /// Apply epoch to the database
    /// @param epoch to write to the database [in]
    /// @param transaction transaction [in]
    //void ApplyUpdates(const Epoch&, const logos::transaction&);

    /// Build Epoch block
    /// @param block build the block [in|out]
    bool Build(Epoch&);

private:
    //BlockStore &            _store;           ///< reference to block store
    EpochVotingManager &    _voting_manager;  ///< voting manager
    //Log                     _log;             ///< boost log reference
    ReservationsProvider    _reservations;
};