///
/// @file
/// This file contains epoch transition related declarations
///
#pragma once

#include <string>

enum class EpochTransitionState
{
    Connecting,
    EpochTransitionStart,
    EpochStart,
    None
};

inline std::string
TransitionStateToName(const EpochTransitionState &state)
{
    switch(state)
    {
        case EpochTransitionState::Connecting:
            return "Connecting";
        case EpochTransitionState::EpochTransitionStart:
            return "EpochTransitionStart";
        case EpochTransitionState::EpochStart:
            return "EpochStart";
        case EpochTransitionState::None:
            return "None";
    }
}

enum class EpochTransitionDelegate
{
    New,
    Persistent,
    Retiring,
    None
};

inline std::string
TransitionDelegateToName(const EpochTransitionDelegate &delegate)
{
    switch(delegate)
    {
        case EpochTransitionDelegate::New:
            return "New";
        case EpochTransitionDelegate::Persistent:
            return "Persistent";
        case EpochTransitionDelegate::Retiring:
            return "Retiring";
        case EpochTransitionDelegate::None:
            return "None";
    }
}

// Represents two sets of delegates during
// Epoch transition
enum class ConnectingDelegatesSet : uint8_t
{
    Current,
    New,
    Outgoing
};

inline std::string
DelegatesSetToName(ConnectingDelegatesSet set)
{
    switch (set)
    {
        case ConnectingDelegatesSet::Current:
            return "Current";
        case ConnectingDelegatesSet::New:
            return "New";
        case ConnectingDelegatesSet::Outgoing:
            return "Outgoing";
    }
}