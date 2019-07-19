/// @file
/// This file contains trace function for printing backtrace when the software halts

#pragma once
#include <execinfo.h>
#include <logos/lib/log.hpp>

inline void trace_and_halt()
{
    const int BUFF_SIZE = 100;
    void *buffer[BUFF_SIZE];
    int nptrs;
    char **strings;
    Log log;

    nptrs = backtrace(buffer, BUFF_SIZE);
    strings = backtrace_symbols(buffer, nptrs);
    if (strings != nullptr)
    {
        for (int i = 0; i < nptrs; ++i)
        {
            LOG_FATAL(log) << strings[i];
        }
        free(strings);
        assert(false);
        std::exit(EXIT_FAILURE);
    }
}

inline void trace_and_dont_halt()
{
    const int BUFF_SIZE = 100;
    void *buffer[BUFF_SIZE];
    int nptrs;
    char **strings;
    Log log;

    nptrs = backtrace(buffer, BUFF_SIZE);
    strings = backtrace_symbols(buffer, nptrs);
    if (strings != nullptr)
    {
        for (int i = 0; i < nptrs; ++i)
        {
            LOG_FATAL(log) << strings[i];
        }
        free(strings);
    }

}
