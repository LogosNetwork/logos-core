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
};

class Persistence
{

protected:

    using Store        = logos::block_store;
    using Milliseconds = std::chrono::milliseconds;

public:

    static constexpr Milliseconds DEFAULT_CLOCK_DRIFT = Milliseconds(20000);
    static constexpr Milliseconds ZERO_CLOCK_DRIFT = Milliseconds(0);

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

    Store &             _store;
    Log                 _log;
    Milliseconds        _clock_drift;
};
