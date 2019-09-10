///
/// @file
/// This file contains the declaration of Epoch and Microblock-related time utilities
///
#pragma once

#include <stdint.h>
#include <mutex>
#include <functional>
#include <boost/asio/deadline_timer.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <logos/consensus/messages/common.hpp>

using Milliseconds = std::chrono::milliseconds;
using Seconds = std::chrono::seconds;
using Minutes = std::chrono::minutes;
using Hours = std::chrono::hours;

using Clock = boost::asio::deadline_timer::traits_type;
using TimePoint  = boost::posix_time::ptime;
static const TimePoint Min_DT = TimePoint(boost::posix_time::min_date_time);

//#define FAST_MB_EB_TEST
#ifndef FAST_MB_EB_TEST
/// Epoch transition starts every 12 hours
/// Epoch events
/// 1. New delegates set connection time 12h - 5m
static const Minutes EPOCH_DELEGATES_CONNECT(5); // 5 minutes
/// 2. Epoch transition start time: 12h - 20s
static const Seconds EPOCH_TRANSITION_START(20); // 20 seconds
/// 3. Epoch start time: 12h
static const Hours EPOCH_PROPOSAL_TIME(12); // 12 hours
/// 4. Epoch transition end time: 12h + 20s
static const Seconds EPOCH_TRANSITION_END(20); // 20 seconds
static const Minutes MICROBLOCK_PROPOSAL_TIME(10); // 10 minutes
static const Minutes MICROBLOCK_CUTOFF_TIME(10); // 10 minutes
static const Seconds CLOCK_DRIFT(20); // 20 seconds
static const Seconds SECONDARY_LIST_TIMEOUT(20); // 20 seconds
static const Seconds ARCHIVAL_TIMEOUT_SEMI_IDLE(60); // 1 minute
static const Seconds ARCHIVAL_TIMEOUT_IDLE(600); // 10 minutes
static const Minutes SECONDARY_LIST_TIMEOUT_CAP(8); // 8 minutes
static const Seconds PRIMARY_TIMEOUT(60);
static const Seconds RECALL_TIMEOUT(300);

#else

/// Epoch events
/// 1. Epoch start time:
static const Minutes EPOCH_PROPOSAL_TIME(20);
/// 2. New delegates set connection time 20m - 2m
static const Minutes EPOCH_DELEGATES_CONNECT(2);
/// 3. Epoch transition start time: 20m - 20s
static const Seconds EPOCH_TRANSITION_START(20);
/// 4. Epoch start time: 20 seconds after epoch trans start
static const Seconds EPOCH_START(20);
/// 5. Epoch transition end time: 20 seconds after epoch start
static const Seconds EPOCH_TRANSITION_END(20);
static const Minutes MICROBLOCK_PROPOSAL_TIME(4);
static const Minutes MICROBLOCK_CUTOFF_TIME(4);
static const Seconds CLOCK_DRIFT(20); // 20 seconds
static const Seconds SECONDARY_LIST_TIMEOUT(20); // 20 seconds
static const Minutes SECONDARY_LIST_TIMEOUT_CAP(8); // 8 minutes

#endif

template<typename C, typename T>
C TConvert(T t)
{
    return std::chrono::duration_cast<C>(t);
}

/// Interface for getting epoch and microblock times
class TimeUtil
{
public:
    virtual ~TimeUtil() = default;
    virtual Milliseconds GetNextMicroBlockTime(uint8_t skip) = 0;
    virtual Milliseconds GetNextEpochTime(uint8_t skip) = 0;
};

/// Defines times for epoch and microblock events
class EpochTimeUtil : public TimeUtil
{
public:
    /// Class constructor
    EpochTimeUtil() = default;
    ~EpochTimeUtil() override = default;

    /// Get time for the next microblock construction
    /// @param skip number of proposal intervals, needed in case of a recall or first block [in]
    /// @returns time lapse in seconds for the next microblock event
    Milliseconds GetNextMicroBlockTime(uint8_t skip) override;

    /// Get time for the next epoch construction/transition
    /// @param skip number of proposal intervals, needed in case of a recall or first block [in]
    /// @returns time lapse in seconds for the next epoch event
    Milliseconds GetNextEpochTime(uint8_t skip) override;

    /// Returns the number of seconds to wait until reproposing
    template <ConsensusType CT>
    static long GetTimeout(uint8_t num_proposals, uint8_t delegate_id=0);

private:
    /// Get next timeout value
    /// @param timeout value [in]
    /// @param skip number of proposal intervals, needed in case of a recall or first block [in]
    /// @returns time lapse in seconds for the next epoch event
    template<typename T>
    Milliseconds GetNextTime(T timeout, uint8_t skip);
};

class ArchivalTimer
{
friend void SetTestTimeUtil(uint64_t);

private:
    static std::shared_ptr<TimeUtil> _util_instance;
    static std::shared_ptr<TimeUtil> GetInstance();

public:
    static Milliseconds GetNextMicroBlockTime(uint8_t skip=0);
    static Milliseconds GetNextEpochTime(uint8_t skip=0);

    /// Is this at or past epoch time (> 12h boundary  + 10min MB time - clock drift)
    /// @returns true if it is at or past epoch construction/transition time
    static bool IsPastEpochBlockTime();
};
