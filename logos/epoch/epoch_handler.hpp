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

/// EpochHandler builds, validates, persists, triggers the Epoch
class EpochHandler
{
    using Log           = boost::log::sources::logger_mt;
public:
    /// Class constructor
    /// @param s logos::block_store reference [in]
    EpochHandler(BlockStore &s,
                 uint8_t delegate_id,
                 EpochVotingManager & voting_manager)
        : _store(s)
        , _delegate_id(delegate_id)
        , _voting_manager(voting_manager)
        {}
    virtual ~EpochHandler() {}

    /// Validate Epoch block
    /// @param block block to validate [in]
    bool Validate(const Epoch&);

    /// Persist Epoch block
    /// @param block save the block to the database [in]
    void ApplyUpdates(const Epoch&);

    /// Apply epoch to the database
    /// @param epoch to write to the database [in]
    /// @param transaction transaction [in]
    /// @returns hash of the epoch
    logos::block_hash ApplyUpdates(const Epoch&, const logos::transaction&);

    /// Build Epoch block
    /// @param block build the block [in|out]
    bool Build(Epoch&);

private:
    BlockStore &            _store;          ///< reference to block store
    uint8_t                 _delegate_id;    ///< delegate id
    EpochVotingManager &    _voting_manager; ///< voting manager
    Log                     _log;            ///< boost log reference
};