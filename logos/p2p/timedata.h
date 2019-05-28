// Copyright (c) 2014-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TIMEDATA_H
#define BITCOIN_TIMEDATA_H

#include <algorithm>
#include <assert.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <util.h>
#include <ui_interface.h>
#include <netaddress.h>

#define BITCOIN_TIMEDATA_MAX_SAMPLES 200

constexpr int64_t DEFAULT_MAX_TIME_ADJUSTMENT = 70 * 60;

/**
 * Median filter over a stream of values.
 * Returns the median of the last N numbers
 */
template <typename T>
class CMedianFilter
{
private:
    std::vector<T>  vValues;
    std::vector<T>  vSorted;
    unsigned int    nSize;

public:
    CMedianFilter(unsigned int _size,
                  T initial_value)
        : nSize(_size)
    {
        vValues.reserve(_size);
        vValues.push_back(initial_value);
        vSorted = vValues;
    }

    void input(T value)
    {
        if (vValues.size() == nSize)
            vValues.erase(vValues.begin());
        vValues.push_back(value);

        vSorted.resize(vValues.size());
        std::copy(vValues.begin(), vValues.end(), vSorted.begin());
        std::sort(vSorted.begin(), vSorted.end());
    }

    T median() const
    {
        int vSortedSize = vSorted.size();
        assert(vSortedSize > 0);
        if (vSortedSize & 1) // Odd number of elements
        {
            return vSorted[vSortedSize / 2];
        }
        else // Even number of elements
        {
            return (vSorted[vSortedSize / 2 - 1] + vSorted[vSortedSize / 2]) / 2;
        }
    }

    int size() const
    {
        return vValues.size();
    }

    std::vector<T> sorted() const
    {
        return vSorted;
    }
};

/** Functions to keep track of adjusted P2P time */
class TimeData
{
private:
    CCriticalSection        cs_nTimeOffset;
    int64_t                 nTimeOffset GUARDED_BY(cs_nTimeOffset);
    std::set<CNetAddr>      setKnown;
    CMedianFilter<int64_t>  vTimeOffsets;
    bool fDone;
    std::atomic<int64_t>    nMockTime; //!< For unit testing
public:
    BCLog::Logger &         logger_;
    TimeData(BCLog::Logger &logger)
        : nTimeOffset(0)
        , vTimeOffsets(BITCOIN_TIMEDATA_MAX_SAMPLES, 0)
        , fDone(false)
        , nMockTime(0)
        , logger_(logger)
    {
    }
    int64_t GetTimeOffset();
    int64_t GetAdjustedTime();
    void AddTimeData(ArgsManager &Args, CClientUIInterface &uiInterface, const CNetAddr& ip, int64_t nTime);

    /**
     * GetTimeMicros() and GetTimeMillis() both return the system time, but in
     * different units. GetTime() returns the system time in seconds, but also
     * supports mocktime, where the time can be specified by the user, eg for
     * testing (eg with the setmocktime rpc, or -mocktime argument).
     *
     * TODO: Rework these functions to be type-safe (so that we don't inadvertently
     * compare numbers with different units, or compare a mocktime to system time).
     */

    int64_t GetTime();
    void SetMockTime(int64_t nMockTimeIn);
    int64_t GetMockTime();
};

int64_t GetTimeMillis();
int64_t GetTimeMicros();
int64_t GetSystemTimeInSeconds(); // Like GetTime(), but not mockable
void MilliSleep(int64_t n);

/**
     * ISO 8601 formatting is preferred. Use the FormatISO8601{DateTime,Date,Time}
     * helper functions if possible.
     */
std::string FormatISO8601DateTime(int64_t nTime);
std::string FormatISO8601Date(int64_t nTime);
std::string FormatISO8601Time(int64_t nTime);

#endif // BITCOIN_TIMEDATA_H
