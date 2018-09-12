/// @file
/// This file contains declaration of the EpochHandler class, which is used
/// in the Epoch processing
#pragma once

#include <logos/epoch/epoch.hpp>

namespace logos {
    class block_store;
    class transaction;
}

using BlockStore = logos::block_store;

/// EpochHandler builds, validates, persists, triggers the Epoch
class EpochHandler 
{
public:
    /// Class constructor
    /// @param s logos::block_store reference [in]
    EpochHandler(BlockStore &s)
        : _store(s)
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
    void BuildEpochBlock(Epoch&);

private:
    BlockStore &    _store;          ///< reference to block store
};