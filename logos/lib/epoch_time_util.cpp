///
/// @file
/// This file contains definition of EpochTimeUtil - epoch time related functions
///
#include <logos/lib/epoch_time_util.hpp>
#include <logos/node/node.hpp>
#include <logos/consensus/messages/common.hpp>

template<typename T>
Milliseconds
EpochTimeUtil::GetNextTime(T timeout, bool skip)
{
    auto now = GetStamp();
    auto mult = (skip) ? 2 : 1;
    auto timeout_msec = TConvert<Milliseconds>(timeout).count();
    auto rem = now % timeout_msec;

    return (rem != 0) ? Milliseconds(timeout_msec * mult - rem) : Milliseconds(0);
}

Milliseconds
EpochTimeUtil::GetNextEpochTime(
    bool skip )
{
    return GetNextTime(EPOCH_PROPOSAL_TIME, skip);
}

/// Microblock proposal happens on 10 min boundary
Milliseconds
EpochTimeUtil::GetNextMicroBlockTime(
    bool skip)
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
