/// @file
/// This file contains declaration of the EpochHandler class, which is used
/// in the Epoch processing
#pragma once

#include <logos/epoch/epoch.hpp>

/// EpochHandler builds, validates, persists, triggers the Epoch
class EpochHandler 
{
public:
    EpochHandler() {}
    virtual ~EpochHandler() {}

    /// Validate Epoch block
    /// @param block block to validate [in]
    bool Validate(const Epoch&);

    /// Persist Epoch block
    /// @param block save the block to the database [in]
    void ApplyUpdates(const Epoch&);

    /// Build Epoch block
    /// @param block build the block [in|out]
    void BuildEpochBlock(Epoch&);

private:
};