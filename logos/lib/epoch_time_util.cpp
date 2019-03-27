///
/// @file
/// This file contains definition of EpochTimeUtil - epoch time related functions
///
#include <logos/lib/epoch_time_util.hpp>
#include <logos/node/node.hpp>
#include <logos/consensus/messages/common.hpp>

template<typename T>
Milliseconds
EpochTimeUtil::GetNextTime(T timeout, uint8_t skip)
{
    auto now = GetStamp();
    auto timeout_msec = TConvert<Milliseconds>(timeout).count();
    auto rem = now % timeout_msec;

    if (rem != 0)
    {
        return  Milliseconds(timeout_msec * (skip +1) - rem);
    }
    else if (skip != 0)
    {
        return Milliseconds(timeout_msec * skip);
    }
    else
    {
        return Milliseconds(0);
    }
}

Milliseconds
EpochTimeUtil::GetNextEpochTime(
        uint8_t skip)
{
    return GetNextTime(EPOCH_PROPOSAL_TIME, skip);
}

/// Microblock proposal happens on 10 min boundary
Milliseconds
EpochTimeUtil::GetNextMicroBlockTime(
        uint8_t skip)
{
    return GetNextTime(MICROBLOCK_PROPOSAL_TIME, skip);
}

bool
EpochTimeUtil::IsEpochTime()
{
    auto now = GetStamp();
    auto epoch = TConvert<Milliseconds>(EPOCH_PROPOSAL_TIME).count();
    auto rem = now % epoch;
    auto min = TConvert<Milliseconds>(MICROBLOCK_PROPOSAL_TIME - CLOCK_DRIFT).count();
    auto max = TConvert<Milliseconds>(MICROBLOCK_PROPOSAL_TIME + CLOCK_DRIFT).count();

    return (rem > min && rem < max);
}

bool
EpochTimeUtil::IsOneMBPastEpochTime()
{
    auto now = GetStamp();
    auto epoch = TConvert<Milliseconds>(EPOCH_PROPOSAL_TIME).count();
    auto rem = now % epoch;
    auto min = TConvert<Milliseconds>(MICROBLOCK_PROPOSAL_TIME * 2 - CLOCK_DRIFT).count();
    auto max = TConvert<Milliseconds>(MICROBLOCK_PROPOSAL_TIME * 2 + CLOCK_DRIFT).count();

    return (rem > min && rem < max);
}