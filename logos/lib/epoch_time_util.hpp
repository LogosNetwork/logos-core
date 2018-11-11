///
/// @file
/// This file contains the declaration of Epoch and Microblock-related time utilities
///
#pragma once

#include <stdint.h>
#include <mutex>
#include <functional>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>

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
static const Minutes EPOCH_DELEGATES_CONNECT(1); // 5 minunes
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
    /// @param skip if true then skip the closest time, needed in case of a recall or first block [in]
    /// @returns time lapse in milliseconds for the next microblock event
    std::chrono::seconds GetNextMicroBlockTime(bool skip=false);

    /// Get time for the next epoch construction/transition
    /// @param skip if true then skip the closest time, needed in case of a recall or first block [in]
    /// @returns time lapse in milliseconds for the next epoch event
    std::chrono::seconds GetNextEpochTime(bool skip=false);

    /// Is this epoch time (12h boundary +- clock drift)
    /// @returns true if it is epoch construction/transition time
    bool IsEpochTime();

private:
    /// Get time for the next time boundary
    /// @param skip if true then skip the closest time, needed in case of a recall or first block [in]
    /// @param proposal_sec lapse time in seconds of the proposal time
    /// @param init call back to update tm structure for specific boundary
    /// @returns time lapse in milliseconds for the next epoch event
    std::chrono::seconds GetNextTime(bool skip, int proposal_sec, std::function<void(struct tm&)> init);
};

