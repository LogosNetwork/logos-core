///
/// @file
/// This file contains the declaration of Epoch and Microblock-related time utilities
///
#pragma once

#include <stdint.h>
#include <mutex>
#include <functional>

using Milliseconds = std::chrono::milliseconds;
using Seconds = std::chrono::seconds;
using Minutes = std::chrono::minutes;
using Hours = std::chrono::hours;

/// Epoch transition starts every 12 hours
/// Epoch events
/// 1. Epoch start time: 12h
static const Hours EPOCH_PROPOSAL_TIME(12); // 12 hours
//static const Minutes EPOCH_PROPOSAL_TIME(5);
/// 2. New delegates set connection time 12h - 5m
static const Minutes EPOCH_DELEGATES_CONNECT(5); // 5 minunes
/// 3. Epoch transition start time: 12h - 20s
static const Seconds EPOCH_TRANSITION_START(20); // 20 seconds
/// 4. Epoch start time: 12h
static const Seconds EPOCH_START(20); // 20 seconds after epoch trans start
/// 5. Epoch transition end time: 12h + 20s
static const Seconds EPOCH_TRANSITION_END(20); // 20 seconds
static const Minutes MICROBLOCK_PROPOSAL_TIME(10); // 10 minutes
static const Minutes MICROBLOCK_CUTOFF_TIME(10); // 10 minutes
static const Seconds CLOCK_DRIFT(20); // 20 seconds
static const Seconds SECONDARY_LIST_TIMEOUT(20); // 20 seconds
static const Minutes SECONDARY_LIST_TIMEOUT_CAP(8); // 8 minutes

template<typename C, typename T>
C TConvert(T t)
{
    return std::chrono::duration_cast<C>(t);

}

/// Defines times for epoch and microblock events
class EpochTimeUtil {
public:
    /// Class constructor
    EpochTimeUtil() = default;
    ~EpochTimeUtil() = default;

    /// Get time for the next microblock construction
    /// @param skip number of proposal intervals, needed in case of a recall or first block [in]
    /// @returns time lapse in seconds for the next microblock event
    Milliseconds GetNextMicroBlockTime(uint8_t skip=0);

    /// Get time for the next epoch construction/transition
    /// @param skip number of proposal intervals, needed in case of a recall or first block [in]
    /// @returns time lapse in seconds for the next epoch event
    Milliseconds GetNextEpochTime(uint8_t skip=0);

    /// Is this epoch time (12h boundary +- clock drift)
    /// @returns true if it is epoch construction/transition time
    bool IsEpochTime();
    bool IsOneMBPastEpochTime();
private:
    /// Get next timeout value
    /// @param timeout value [in]
    /// @param skip number of proposal intervals, needed in case of a recall or first block [in]
    /// @returns time lapse in seconds for the next epoch event
    template<typename T>
    Milliseconds GetNextTime(T timeout, uint8_t skip);
};