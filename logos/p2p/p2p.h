// Copyright (c) 2018 Logos Network
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file contains external interface for p2p subsystem based on bitcoin project

#ifndef _P2P_H_INCLUDED
#define _P2P_H_INCLUDED

#include <functional>

class p2p_internal;

#ifndef MDB_VERSION_FULL
struct MDB_env;
typedef unsigned int MDB_dbi;
#endif

struct p2p_config {
	int argc;
	char **argv;
	struct MDB_env *lmdb_env;
	MDB_dbi lmdb_dbi;
	void *boost_io_service;
	std::function<void(std::function<void()> const &, unsigned)> scheduleAfterMs;
	std::function<void(const char *)> init_print;
};

enum p2p_option_flags {
	P2P_OPTION_ARGUMENT	= 1,
	P2P_OPTION_MULTI	= 2,
};

class p2p_interface {
private:
	p2p_internal *p2p;
public:
	p2p_interface() : p2p(0) {}
	~p2p_interface(){ Shutdown(); }
	bool Init(p2p_config &config);
	void Shutdown();
	bool PropagateMessage(const void *message, unsigned size);
	virtual bool ReceiveMessageCallback(const void *message, unsigned size) { return false; }
	static void TraverseCommandLineOptions(std::function<void(const char *option, const char *description, int flags)> callback);
};

#endif
