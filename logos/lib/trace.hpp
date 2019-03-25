/// @file
/// This file contains trace function for printing backtrace when the software halts

#pragma once
#include <execinfo.h>
#include <logos/lib/log.hpp>
#include <iostream>

inline void trace()
{
    const int BT_BUF_SIZE = 100;
    int j, nptrs;
    void *buffer[BT_BUF_SIZE];
    char **strings;

    nptrs = backtrace(buffer, BT_BUF_SIZE);
    printf("backtrace() returned %d addresses\n", nptrs);

    /* The call backtrace_symbols_fd(buffer, nptrs, STDOUT_FILENO)
       would produce similar output to the following: */

    strings = backtrace_symbols(buffer, nptrs);
    if (strings == NULL) {
        perror("backtrace_symbols");
        std::cout << " EXIT_FAILURE<4> " << std::endl;
        //exit(EXIT_FAILURE); // RGD
    }

    for (j = 0; j < nptrs; j++)
        printf("%s\n", strings[j]);

    free(strings);
}

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
        std::cout << " EXIT_FAILURE<5> " << std::endl;
        //std::exit(EXIT_FAILURE); // RGD
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
