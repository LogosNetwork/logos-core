#include <iostream>
#include <atomic>

//Components of the Boost Library
#include <boost/array.hpp>
#include <boost/asio.hpp>

using namespace std;

/**
*  A Network Time Protocol Client that queries the DateTime from the Time Server located at hostname
*/
class NTPClient {

private:
    string               _host_name;
    unsigned short       _port;

    std::atomic<time_t>  _ntp_time; // NTP time (in UNIX Format).
    std::atomic<time_t>  _delay;    // Delay.

    // RequestDatetime_UNIX_s:
    // Requests the date time in UNIX format.
    // static, meant to be called from a thread.
    static time_t RequestDatetime_UNIX_s(NTPClient *this_l);

    // timeout_s:
    // Static, this is the timeout thread which will timeout
    // if we don't get an ntp response fast enough.
    // meant to be called from a thread.
    static void timeout_s(NTPClient *this_l);

    // start_s:
    // Starts the process of getting ntp time every hour.
    // meant to be called from a thread.
    static void start_s(NTPClient *this_l);

public:

    static constexpr int MAX_TIMEOUT = 10; // 5 seconds, user configurable.

    inline
    time_t getNtpTime() const
    {
        return _ntp_time;
    }

    inline
    time_t getDelay() const
    {
        return _delay;
    }

    inline
    void setNtpTime(time_t ntp)
    {
        _ntp_time = ntp;
    }

    inline
    void setDelay(time_t delay)
    {
        _delay = delay;
    }

    // **********************
    // **** Start of API ****
    // **********************

    // CTOR
    NTPClient(string i_hostname);

    // RequestDatetime_UNIX:
    // Requests the date time in UNIX format.
    // non-static, non-async
    time_t RequestDatetime_UNIX();

    // init:
    // Start async requests for ntp time
    // runs a loop in a seperate thread obtaining the ntp time
    // every hour. returns delta() on initial run.
    time_t init();

    // computeDelta:
    // Compute delta as difference between local time and ntp time.
    time_t computeDelta();

    // getCurrentDelta:
    // Delta from previous calculation
    time_t getCurrentDelta();

    // now:
    // The time now including the difference from delta.
    time_t now();

    // to_string:
    // Converts ntp time to a readable string based on format.
    inline
    std::string to_string(const char * format = "%a %b %d %Y %T", time_t t = 0) const
    {
        char date[128]={0};
        struct tm lt={0};
        localtime_r(&t,&lt);
        strftime(date,sizeof(date),format,&lt);
        return std::string(date);
    }

    // asyncNTP:
    // Makes an async request to get the ntp time,
    // if the timeout expires, it returns with time set to 0.
    // Otherwise, time is set to ntp time.
    void asyncNTP();

    // getTime:
    // Returns the ntp time.
    time_t getTime();

    // timedOut:
    // Returns true if we timed out (indicated by 0 value for ntp time)
    bool timedOut();

    // getDefault:
    // Returns current local time.
    time_t getDefault();
};
