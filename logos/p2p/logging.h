// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LOGGING_H
#define BITCOIN_LOGGING_H

#include <fs.h>
#include <tinyformat.h>

#include <atomic>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <vector>
#include "../lib/log.hpp"

static const bool DEFAULT_LOGIPS        = false;
extern bool fLogIPs;

struct CLogCategoryActive
{
    std::string category;
    bool active;
};

namespace BCLog {
    enum LogFlags : uint32_t {
        NONE        = 0,
        NET         = (1 <<  0),
        TOR         = (1 <<  1),
        MEMPOOL     = (1 <<  2),
        HTTP        = (1 <<  3),
        BENCH       = (1 <<  4),
	DB          = (1 <<  5),
	RPC         = (1 <<  6),
	ESTIMATEFEE = (1 <<  7),
	ADDRMAN     = (1 <<  8),
	SELECTCOINS = (1 <<  9),
	REINDEX     = (1 << 10),
	CMPCTBLOCK  = (1 << 11),
	RAND        = (1 << 12),
	PRUNE       = (1 << 13),
	PROXY       = (1 << 14),
	MEMPOOLREJ  = (1 << 15),
	LIBEVENT    = (1 << 16),
	COINDB      = (1 << 17),
	QT          = (1 << 18),
	LEVELDB     = (1 << 19),
        ALL         = ~(uint32_t)0,
    };

    class Logger
    {
    private:
        std::list<std::string> m_msgs_before_open;

        /**
         * m_started_new_line is a state variable that will suppress printing of
         * the timestamp when multiple calls are made that don't end in a
         * newline.
         */
        std::atomic_bool m_started_new_line{true};

        /** Log categories bitfield. */
        std::atomic<uint32_t> m_categories{0};

    public:
	Log log;

        /** Send a string to the log output */
	void LogPrintStr(boost::log::trivial::severity_level level, const std::string &str);

        /** Returns whether logs will be written to any output */
	bool Enabled() const { return true; }

        uint32_t GetCategoryMask() const { return m_categories.load(); }

        void EnableCategory(LogFlags flag);
        bool EnableCategory(const std::string& str);
        void DisableCategory(LogFlags flag);
        bool DisableCategory(const std::string& str);

        bool WillLogCategory(LogFlags category) const;

	bool DefaultShrinkDebugFile() const;
    };

} // namespace BCLog

extern BCLog::Logger* const g_logger;

/** Return true if log accepts specified category */
static inline bool LogAcceptCategory(BCLog::LogFlags category)
{
    return g_logger->WillLogCategory(category);
}

/** Returns a string with the log categories. */
std::string ListLogCategories();

/** Returns a vector of the active log categories. */
std::vector<CLogCategoryActive> ListActiveLogCategories();

/** Return true if str parses as a log category and set the flag */
bool GetLogCategory(BCLog::LogFlags& flag, const std::string& str);

/** Get format string from VA_ARGS for error reporting */
template<typename... Args> std::string FormatStringFromLogArgs(const char *fmt, const Args&... args) { return fmt; }

static inline void MarkUsed() {}
template<typename T, typename... Args> static inline void MarkUsed(const T& t, const Args&... args)
{
    (void)t;
    MarkUsed(args...);
}

// Be conservative when using LogPrintf/error or other things which
// unconditionally log to debug.log! It should not be the case that an inbound
// peer can fill up a user's disk with debug.log entries.

#ifdef USE_COVERAGE
#define LogPrintf(...) do { MarkUsed(__VA_ARGS__); } while(0)
#define LogPrint(category, ...) do { MarkUsed(__VA_ARGS__); } while(0)
#else
#define LogPrintfSeverity(severity, ...) do { \
    if (g_logger->Enabled()) { \
        std::string _log_msg_; /* Unlikely name to avoid shadowing variables */ \
        try { \
            _log_msg_ = tfm::format(__VA_ARGS__); \
        } catch (tinyformat::format_error &fmterr) { \
            /* Original format string will have newline so don't add one here */ \
            _log_msg_ = "Error \"" + std::string(fmterr.what()) + "\" while formatting log message: " + FormatStringFromLogArgs(__VA_ARGS__); \
        } \
	g_logger->LogPrintStr(severity, _log_msg_); \
    } \
} while(0)

#define LogPrintSeverity(severity, category, ...) do { \
    if (LogAcceptCategory((category))) { \
	LogPrintfSeverity(severity, __VA_ARGS__); \
    } \
} while(0)

#define LogTrace(category, ...)   LogPrintSeverity(boost::log::trivial::trace,   category, __VA_ARGS__)
#define LogDebug(category, ...)   LogPrintSeverity(boost::log::trivial::debug,   category, __VA_ARGS__)
#define LogInfo(category, ...)    LogPrintSeverity(boost::log::trivial::info,    category, __VA_ARGS__)
#define LogWarning(category, ...) LogPrintSeverity(boost::log::trivial::warning, category, __VA_ARGS__)
#define LogError(category, ...)   LogPrintSeverity(boost::log::trivial::error,   category, __VA_ARGS__)

#define LogPrintf(...) LogPrintfSeverity(boost::log::trivial::info, __VA_ARGS__)
#define LogPrint LogInfo
#endif

#endif // BITCOIN_LOGGING_H
