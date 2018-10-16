///
/// @file
/// This file contains definition of EpochTimeUtil - epoch time related functions
///
#include <logos/lib/epoch_time_util.hpp>
#include <logos/node/node.hpp>

std::chrono::seconds
EpochTimeUtil::GetNextTime(
   bool skip,
   int proposal_sec,
   function<void(struct tm &)> init)
{
    time_t rawtime;
    struct tm gmt;
    auto totime = [](struct tm *gmt)mutable->time_t{
        struct tm tmp;
        memcpy(&tmp, gmt, sizeof(tmp));
        return mktime(&tmp);
    };

    time(&rawtime);
    assert(gmtime_r(&rawtime, &gmt) != NULL);
    time_t t1 = totime(&gmt);

    // move the clock forward by required time lapse (10 or 20 minutes micro block or 12 epoch block)
    rawtime += proposal_sec * (skip ? 2 : 1);
    assert(gmtime_r(&rawtime, &gmt) != NULL);

    // Find the previous 10 min or 12 hour boundary - event time
    init(gmt);

    // Convert event time to local time
    time_t t2 = totime(&gmt);

    return std::chrono::seconds(t2 - t1);
}

std::chrono::seconds
EpochTimeUtil::GetNextEpochTime(
    bool skip )
{
    static int sec = TConvert<Seconds>(EPOCH_PROPOSAL_TIME).count();

    return GetNextTime(skip, sec, [](struct tm &gmt)mutable->void{
        gmt.tm_hour = gmt.tm_hour - (gmt.tm_hour % EPOCH_PROPOSAL_TIME.count());
        gmt.tm_min = 0;
        gmt.tm_sec = 0;
    });
}

/// Microblock proposal happens on 10 min boundary
std::chrono::seconds
EpochTimeUtil::GetNextMicroBlockTime(
    bool skip)
{
    static int sec = TConvert<Seconds>(MICROBLOCK_PROPOSAL_TIME).count();

    return GetNextTime(skip, sec, [](struct tm &gmt)mutable->void{
        gmt.tm_min = gmt.tm_min - (gmt.tm_min % MICROBLOCK_PROPOSAL_TIME.count());
        gmt.tm_sec = 0;
    });
}

bool
EpochTimeUtil::IsEpochTime()
{
    time_t rawtime;
    struct tm gmt;
    static int min = TConvert<Seconds>(MICROBLOCK_PROPOSAL_TIME - CLOCK_DRIFT).count();
    static int max = TConvert<Seconds>(MICROBLOCK_PROPOSAL_TIME + CLOCK_DRIFT).count();

    time(&rawtime);
    assert(gmtime_r(&rawtime, &gmt) != NULL);

    // Epoch is proposed after the last Microblock
    // which is proposed retroactively at 00h:10m(GMT) +- 40sec or 12h:10m(GMT) +- 40sec
    int ms = gmt.tm_min * 60 + gmt.tm_sec;
    return (gmt.tm_hour == 0 || gmt.tm_hour == 12) && (ms > min && ms < max);
}
