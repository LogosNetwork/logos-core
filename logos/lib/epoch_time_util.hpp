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

static const uint16_t EPOCH_PROPOSAL_TIME = 12; // 12 hours
static const uint16_t MICROBLOCK_PROPOSAL_TIME = 10; // 10 minutes
static const uint16_t MICROBLOCK_CUTOFF_TIME = 10; // 10 minutes
static const uint16_t CLOCK_DRIFT = 20; // 20 seconds

/// Defines times for epoch and microblock events
class EpochTimeUtil {
    using Log        = boost::log::sources::logger_mt;
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

    Log _log; ///< boost asio log
};

