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
    bool Validate(const Epoch&);
    /// Persist Epoch block
    void ApplyUpdates(const Epoch&);

private:
};