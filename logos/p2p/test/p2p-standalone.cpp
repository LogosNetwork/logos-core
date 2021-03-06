// Copyright (c) 2018 Logos Network
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file contains main function of p2p standalone application

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <boost/move/utility_core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/global_logger_storage.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/trivial.hpp>
#include <boost/asio.hpp>
#include "../../../lmdb/libraries/liblmdb/lmdb.h"
#include "../p2p.h"

#define OPTHEAD     26
#define OPTWIDTH    80
#define OPTMAX      4096

class p2p_standalone : public p2p_interface
{
    virtual bool ReceiveMessageCallback(const void *message, unsigned size)
    {
        printf("\nReceived %4d:", size);
        if (size && *(const uint8_t *)message >= ' ')
            printf(" %.*s", size, (const char *)message);
        else for (int i = 0; i < 256 && i < size; ++i)
            printf(" %02x", ((const uint8_t *)message)[i]);
        printf("\nType message: ");
        fflush(stdout);
        return true;
    }
};

extern void RenameThread(const char* name);

static void *io_service_run(void *arg)
{
    RenameThread("p2p-io-service");
    p2p_config *config = (p2p_config *)arg;
    boost::system::error_code ec;
    ((boost::asio::io_service *)config->boost_io_service)->run(ec);
    return 0;
}

struct scheduleData
{
    std::function<void()>   handler;
    unsigned                ms;
};

static void *schedule_run(void *arg)
{
    struct scheduleData *data = (struct scheduleData *)arg;
    usleep((unsigned long)data->ms * 1000);
    data->handler();
    delete data;
    return 0;
}

void scheduleAfterMs(std::function<void()> const &handler, unsigned ms)
{
    struct scheduleData *data = new scheduleData;
    data->handler = handler;
    data->ms = ms;
    pthread_t t;
    pthread_create(&t, 0, schedule_run, data);
    pthread_detach(t);
}

int main(int argc, char **argv)
{
    p2p_standalone p2p;
    p2p_config config;
    MDB_txn *txn;
    char buf[256], *str;
    const char *mess;
    int err;
    pthread_t thread;
    void *res;

    printf("This is p2p standalone application.\n");
    if (argc == 1 || (argc == 2
                      && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "-?") || !strcmp(argv[1], "-help") || !strcmp(argv[1], "--help"))))
    {
        printf("Usage: %s options...\nOptions:\n", argv[0]);
        p2p.TraverseCommandLineOptions([](const char *option, const char *description, int flags)
        {
            char buf[OPTMAX];
            int pos, len, i, c;
            sprintf(buf, "  -%s%-*s", option, static_cast<uint32_t>(OPTHEAD - 3 - strlen(option)), flags & P2P_OPTION_ARGUMENT ? "=arg" : "    ");
            pos = len = strlen(buf);
            while (*description)
            {
                if (pos >= OPTWIDTH)
                {
                    i = 0;
                    if (*description != ' ')
                        while (buf[len - i - 1] != ' ')
                            ++i;
                    if (i)
                        memmove(buf + len + OPTHEAD + 1 - i, buf + len - i, i);
                    c = buf[len + OPTHEAD + 1 - i];
                    len += sprintf(buf + len - i, "\n%*s", OPTHEAD, ""), pos = OPTHEAD;
                    if (i)
                        buf[len - i] = c;

                    if (*description == ' ')
                    {
                        ++description;
                        continue;
                    }
                }
                buf[len++] = *description++;
                pos++;
            }
            buf[len++] = 0;
            printf("%s\n", buf);
        });
        return 1;
    }

    printf("Initializing...\n");
    signal(SIGTTIN, SIG_IGN);
    mkdir(".logos", 0770);

    config.argc = argc;
    config.argv = argv;
    config.test_mode = false;

    boost::log::add_common_attributes ();
    boost::log::register_simple_formatter_factory< boost::log::trivial::severity_level, char > ("Severity");
    boost::log::core::get()->set_filter (boost::log::trivial::severity >= boost::log::trivial::trace);
    boost::log::add_file_log (boost::log::keywords::target = "log",
                              boost::log::keywords::file_name = "log/log_%Y-%m-%d_%H-%M-%S.%N.log",
                              boost::log::keywords::rotation_size = 0x100000,
                              boost::log::keywords::auto_flush = true,
                              boost::log::keywords::scan_method = boost::log::sinks::file::scan_method::scan_matching,
                              boost::log::keywords::max_size = 0x200000,
                              boost::log::keywords::format = "[%TimeStamp% %ThreadID% %Severity%]: %Message%");

    boost::asio::io_service io_service;
    config.boost_io_service = &io_service;
    config.scheduleAfterMs = std::bind(&::scheduleAfterMs, std::placeholders::_1, std::placeholders::_2);
    config.userInterfaceMessage = [](int type, const char *mess)
    {
        printf("\n%s%s: %s\nType message: ", (type & P2P_UI_INIT ? "init " : ""),
               (type & P2P_UI_ERROR ? "error" : type & P2P_UI_WARNING ? "warning" : "message"), mess);
        fflush(stdout);
    };

    err = mdb_env_create(&config.lmdb_env);
    if (err)
    {
        mess = "env create";
        goto fail;
    }
    err = mdb_env_set_maxdbs(config.lmdb_env, 1);
    if (err)
    {
        mess = "set maxdbs";
        goto fail;
    }
    err = mdb_env_open(config.lmdb_env, ".logos", 0, 0644);
    if (err)
    {
        mess = "env open";
        goto fail;
    }
    err = mdb_txn_begin(config.lmdb_env, 0, 0, &txn);
    if (err)
    {
        mess = "txn begin";
        goto fail;
    }
    err = mdb_dbi_open(txn, "p2p_db", MDB_CREATE, &config.lmdb_dbi);
    if (err)
    {
        mess = "dbi open";
        goto fail;
    }
    err = mdb_txn_commit(txn);
    if (err)
    {
        mess = "txn commit";
        goto fail;
    }

    if (!p2p.Init(config))
        return 0;
    pthread_create(&thread, 0, &io_service_run, &config);
    printf("Type 'exit' to exit the program or message to send; other commands: peers, ban host, banned host.\n");

    for(;;)
    {
        printf("Type message: ");
        fflush(stdout);
        if (!fgets(buf, 256, stdin))
            break;
        str = buf;
        while (*str && isspace(*str))
            str++;
        while (*str && isspace(str[strlen(str) - 1]))
            str[strlen(str) - 1] = 0;

        if (!strcmp(str, "exit"))
        {
            break;
        }
        else if (!strcmp(str, "peers"))
        {
            char *node;
            int next = 0;
            while (p2p.get_peers(&next, &node, 1))
            {
                printf("%d. %s\n", next - 1, node);
                free(node);
            }
        }
        else if (!memcmp(str, "ban ", 4))
        {
            p2p.add_to_blacklist(str + 4);
        }
        else if (!memcmp(str, "banned ", 7))
        {
            printf("%s\n", p2p.is_blacklisted(str + 7) ? "yes" : "no");
        }
        else if (*str)
        {
            p2p.PropagateMessage(str, strlen(str), true);
        }
    }

    printf("Shutdown...\n");
    p2p.Shutdown();
    io_service.stop();
    pthread_join(thread, &res);
    printf("Bye-bye!\n");

    return 0;
fail:
    printf("Can't perform operation '%s' with LMDB database, error %d.\n", mess, err);
    return err;
}
