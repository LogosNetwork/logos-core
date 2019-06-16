/// @file
/// This file declares base Persistence class

#pragma once

#include <logos/blockstore.hpp>
#include <logos/lib/log.hpp>

struct ValidationStatus
{
    using Requests = std::unordered_map<uint16_t, logos::process_result>;

    Requests              requests;
    logos::process_result reason;
    uint8_t               progress;
};

class Persistence
{

protected:

    using Store        = logos::block_store;
    using Milliseconds = std::chrono::milliseconds;

public:

    static constexpr Milliseconds DEFAULT_CLOCK_DRIFT = Milliseconds(20000);
    static constexpr Milliseconds ZERO_CLOCK_DRIFT    = Milliseconds(0);

    Persistence(Store & store,
                Milliseconds clock_drift = DEFAULT_CLOCK_DRIFT)
        : _store(store)
        , _clock_drift(clock_drift)
    {}

    virtual ~Persistence() = default;

    static void UpdateStatusRequests(ValidationStatus *status, uint16_t i, logos::process_result result)
    {
        if (status != nullptr)
        {
            status->requests[i] = result;
        }
    }

    static void UpdateStatusReason(ValidationStatus *status, logos::process_result r)
    {
        if (status != nullptr)
        {
            status->reason = r;
        }
    }

protected:

    static logos::uint128_t CalculatePortion(const logos::uint128_t stake,
                                             const logos::uint128_t total_stake,
                                             const logos::uint128_t pool)
    {
        auto numerator = logos::uint256_t(stake) * logos::uint256_t(pool);

        // Calculate the ceiling of the portion of the pool
        // that corresponds to stake / total_stake.
        auto portion = numerator / total_stake
            + (((numerator < 0) ^ (total_stake > 0)) && (numerator % total_stake));

        return portion.convert_to<logos::uint128_t>();
    }

    static bool AdjustRemaining(logos::uint128_t & value, logos::uint128_t remaining)
    {
        if(value == 0)
        {
            value = 1;
        }

        if(value > remaining)
        {
            value = remaining;
        }

        return value > 0;
    }

    bool ValidateTimestamp(uint64_t timestamp)
    {
        auto now = GetStamp();
        auto ts =   timestamp;

        auto drift = now > ts ? now - ts : ts - now;

        if(drift > _clock_drift.count())
        {
            return false;
        }

        return true;
    }

    void PlaceReceive(
        ReceiveBlock & receive,
        uint64_t timestamp,
        MDB_txn * transaction);

    Store &      _store;
    Log          _log;
    Milliseconds _clock_drift;
};
