// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LOGGING_H
#define BITCOIN_LOGGING_H

#include <atomic>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <vector>
#include "../lib/log.hpp"
#include <tinyformat.h>

constexpr bool DEFAULT_LOGIPS        = false;

namespace BCLog
{

enum LogFlags : uint32_t
{
    NONE        = 0,
    NET         = (1 <<  0),
    ADDRMAN     = (1 <<  8),
    ALL         = ~(uint32_t)0,
};

class Logger
{
private:
    /** Log categories bitfield. */
    std::atomic<uint32_t>   m_categories{0};

public:
    Log                     log;

    /** Send a string to the log output */
    void LogPrintStr(boost::log::trivial::severity_level level, const std::string &str);

    /** Returns whether logs will be written to any output */
    bool Enabled() const
    {
        return true;
    }

    void EnableCategory(LogFlags flag);
    bool EnableCategory(const std::string& str);
    void DisableCategory(LogFlags flag);
    bool DisableCategory(const std::string& str);

    bool WillLogCategory(LogFlags category) const;

    /** Return true if log accepts specified category */
    bool LogAcceptCategory(BCLog::LogFlags category)
    {
        return WillLogCategory(category);
    }
};

} // namespace BCLog

/** Returns a string with the log categories. */
std::string ListLogCategories();

/** Return true if str parses as a log category and set the flag */
bool GetLogCategory(BCLog::LogFlags& flag, const std::string& str);

/** Get format string from VA_ARGS for error reporting */
template<typename... Args> std::string FormatStringFromLogArgs(const char *fmt, const Args&... args)
{
    return fmt;
}

// Be conservative when using LogPrintf/error or other things which
// unconditionally log to debug.log! It should not be the case that an inbound
// peer can fill up a user's disk with debug.log entries.

#define LogPrintfSeverity(severity, ...) do \
{ \
    if (logger_.Enabled()) \
    { \
        std::string _log_msg_; /* Unlikely name to avoid shadowing variables */ \
        try \
        { \
            _log_msg_ = tfm::format(__VA_ARGS__); \
        } \
        catch (tinyformat::format_error &fmterr) { \
            /* Original format string will have newline so don't add one here */ \
            _log_msg_ = "Error \"" + std::string(fmterr.what()) + "\" while formatting log message: " + FormatStringFromLogArgs(__VA_ARGS__); \
        } \
        logger_.LogPrintStr(severity, _log_msg_); \
    } \
} \
while(0)

#define LogPrintSeverity(severity, category, ...) do \
{ \
    if (logger_.LogAcceptCategory((category))) \
    { \
        LogPrintfSeverity(severity, __VA_ARGS__); \
    } \
} \
while(0)

#define LogTrace(category, ...)   LogPrintSeverity(boost::log::trivial::trace,   category, __VA_ARGS__)
#define LogDebug(category, ...)   LogPrintSeverity(boost::log::trivial::debug,   category, __VA_ARGS__)
#define LogInfo(category, ...)    LogPrintSeverity(boost::log::trivial::info,    category, __VA_ARGS__)
#define LogWarning(category, ...) LogPrintSeverity(boost::log::trivial::warning, category, __VA_ARGS__)
#define LogError(category, ...)   LogPrintSeverity(boost::log::trivial::error,   category, __VA_ARGS__)

#define LogPrintf(...)            LogPrintfSeverity(boost::log::trivial::info, __VA_ARGS__)
#define LogPrint                  LogInfo

#endif // BITCOIN_LOGGING_H
