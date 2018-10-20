// Copyright (c) 2018 Logos Network
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file contains external interface for p2p subsystem based on bitcoin project

#ifndef _P2P_H_INCLUDED
#define _P2P_H_INCLUDED

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
	void *boost_logger_mt;
	void *boost_io_service;
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
};

#endif
