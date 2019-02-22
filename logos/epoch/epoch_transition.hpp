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
    std::string result;

    switch (connection)
    {
        case EpochConnection::Current:
            result = "Current";
            break;
        case EpochConnection::Transitioning:
            result = "Transition";
            break;
        case EpochConnection::WaitingDisconnect:
            result = "WaitingDisconnect";
            break;
    }

    return result;
}

inline std::string
TransitionStateToName(const EpochTransitionState &state)
{
    std::string result;

    switch(state)
    {
        case EpochTransitionState::Connecting:
            result = "Connecting";
            break;
        case EpochTransitionState::EpochTransitionStart:
            result = "EpochTransitionStart";
            break;
        case EpochTransitionState::EpochStart:
            result = "EpochStart";
            break;
        case EpochTransitionState::None:
            result = "None";
            break;
    }

    return result;
}

inline std::string
TransitionDelegateToName(const EpochTransitionDelegate &delegate)
{
    std::string result;

    switch(delegate)
    {
        case EpochTransitionDelegate::New:
            result = "New";
            break;
        case EpochTransitionDelegate::Persistent:
            result = "Persistent";
            break;
        case EpochTransitionDelegate::PersistentReject:
            result = "PersistentReject";
            break;
        case EpochTransitionDelegate::Retiring:
            result = "Retiring";
            break;
        case EpochTransitionDelegate::RetiringForwardOnly:
            result = "RetiringForwardOnly";
            break;
        case EpochTransitionDelegate::None:
            result = "None";
            break;
    }

    return result;
}
