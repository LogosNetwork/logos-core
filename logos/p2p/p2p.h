// Copyright (c) 2018 Logos Network
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file contains external interface for p2p subsystem based on bitcoin project

#ifndef _P2P_H_INCLUDED
#define _P2P_H_INCLUDED

#include <functional>
#include <memory>

class p2p_internal;

#ifndef MDB_VERSION_FULL
struct MDB_env;
typedef unsigned int MDB_dbi;
#endif

enum userInterfaceMessageTypes
{
    P2P_UI_INFO     = 1,
    P2P_UI_WARNING  = 2,
    P2P_UI_ERROR    = 4,
    P2P_UI_INIT     = 8,
};

struct p2p_config
{
    int                                                             argc;
    char **                                                         argv;
    struct MDB_env *                                                lmdb_env;
    MDB_dbi                                                         lmdb_dbi;
    void *                                                          boost_io_service;
    std::function<void(std::function<void()> const &, unsigned)>    scheduleAfterMs;
    std::function<void(int,const char *)>                           userInterfaceMessage;
    bool                                                            test_mode;
};

enum p2p_option_flags
{
    P2P_OPTION_ARGUMENT = 1,
    P2P_OPTION_MULTI    = 2,
};

class p2p_interface
{
private:
    std::shared_ptr<p2p_internal>   p2p;

public:
    ~p2p_interface()
    {
        Shutdown();
    }

    bool Init(p2p_config &config);
    void Shutdown();
    bool PropagateMessage(const void *message, unsigned size, bool output);

    /* add nodes array to the database; return number of successfully added */
    int add_peers(char **nodes, uint8_t count);

    /* Fills the nodes array of the count size by pointers to subsequent nodes
     * starting from the node with id *next.
     * Returns number of filled nodes, set *next to id of next node to fill.
     */
    int get_peers(int *next, char **nodes, uint8_t count);

    /* Add a peer to a blacklist
     * to be called when validation fails
     */
    void add_to_blacklist(const char *addr);

    /* true if peer is in the blacklist
     * to be checked when we select a new peer to bootstrap from
     */
    bool is_blacklisted(const char *addr);

    /* load peers and blacklist databases from disk; returns true if success */
    bool load_databases();

    /* save peers and blacklist databases to disk; returns true if success */
    bool save_databases();

    virtual bool ReceiveMessageCallback(const void *message, unsigned size)
    {
        return false;
    }

    static void TraverseCommandLineOptions(std::function<void(const char *option, const char *description, int flags)> callback);
};

#endif
