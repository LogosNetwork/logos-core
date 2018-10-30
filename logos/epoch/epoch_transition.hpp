///
/// @file
/// This file contains epoch transition related declarations
///
#pragma once

#include <string>
#include <atomic>
#include <boost/system/error_code.hpp>

enum class EpochTransitionState : uint8_t
{
    Connecting,             // -5min to -20 sec
    EpochTransitionStart,   // -20sec to 00
    EpochStart,             // 00 to + 20sec
    None                    // transition end or no transition
};

enum class EpochTransitionDelegate : uint8_t
{
    New,
    Persistent,
    PersistentReject,
    Retiring,
    RetiringForwardOnly,
    None
};

/// Used to decide what set of delegates to connect and
/// if should reconnect on disconnected socket
enum class EpochConnection : uint8_t
{
    Transitioning,      // Connect to Transitioning delegate's set
    WaitingDisconnect,  // Delegate is waiting to be disconnected (from EpochStart event)
    Current             // Connect to set of delegates when there is no epoch transition, or 'old' set of delegates
};

inline std::string
TransitionConnectionToName(const EpochConnection connection)
{
    switch (connection)
    {
        case EpochConnection::Current:
            return "Current";
        case EpochConnection::Transitioning:
            return "Transition";
        case EpochConnection::WaitingDisconnect:
            return "WaitingDisconnect";
    }
}

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

inline std::string
TransitionDelegateToName(const EpochTransitionDelegate &delegate)
{
    switch(delegate)
    {
        case EpochTransitionDelegate::New:
            return "New";
        case EpochTransitionDelegate::Persistent:
            return "Persistent";
        case EpochTransitionDelegate::PersistentReject:
            return "PersistentReject";
        case EpochTransitionDelegate::Retiring:
            return "Retiring";
        case EpochTransitionDelegate::RetiringForwardOnly:
            return "RetiringForwardOnly";
        case EpochTransitionDelegate::None:
            return "None";
    }
}
