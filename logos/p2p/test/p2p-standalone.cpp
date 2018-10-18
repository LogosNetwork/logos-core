// Copyright (c) 2018 Logos Network
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file contains main function of p2p standalone application

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "../p2p.h"
#include "../../../lmdb/libraries/liblmdb/lmdb.h"
#include <boost/move/utility_core.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/global_logger_storage.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

BOOST_LOG_INLINE_GLOBAL_LOGGER_DEFAULT(my_logger, boost::log::sources::logger_mt)

class p2p_standalone : public p2p_interface {
	virtual bool ReceiveMessageCallback(const void *message, unsigned size) {
		printf("\nReceived %3d: %.*s\nType message: ", size, size, (const char *)message);
		fflush(stdout);
	}
};

int main(int argc, char **argv) {
	p2p_standalone p2p;
	p2p_config config;
	MDB_txn *txn;
	char buf[256], *str;
	const char *mess;
	int err;

	printf("This is p2p standalone application. Initializing...\n");

	config.argc = argc;
	config.argv = argv;
	boost::log::add_common_attributes();
	boost::log::add_file_log (boost::log::keywords::target = "log",
				  boost::log::keywords::file_name = "log/log_%Y-%m-%d_%H-%M-%S.%N.log",
				  boost::log::keywords::rotation_size = 0x100000,
				  boost::log::keywords::auto_flush = true,
				  boost::log::keywords::scan_method = boost::log::sinks::file::scan_method::scan_matching,
				  boost::log::keywords::max_size = 0x200000,
				  boost::log::keywords::format = "[%TimeStamp%]: %Message%");
	boost::log::sources::logger_mt& lg = my_logger::get();
	config.boost_logger_mt = &lg;

	err = mdb_env_create(&config.lmdb_env);
	if (err) { mess = "env create"; goto fail; }
	err = mdb_env_set_maxdbs(config.lmdb_env, 1);
	if (err) { mess = "set maxdbs"; goto fail; }
	err = mdb_env_open(config.lmdb_env, ".logos", 0, 0644);
	if (err) { mess = "env open"; goto fail; }
	err = mdb_txn_begin(config.lmdb_env, 0, 0, &txn);
	if (err) { mess = "txn begin"; goto fail; }
	err = mdb_dbi_open(txn, "p2p_db", MDB_CREATE, &config.lmdb_dbi);
	if (err) { mess = "dbi open"; goto fail; }
	err = mdb_txn_commit(txn);
	if (err) { mess = "txn commit"; goto fail; }

	if (!p2p.Init(config)) return 0;
	printf("Type 'exit' to exit the program or message to send.\n");

	for(;;) {
		printf("Type message: "); fflush(stdout);
		if (!fgets(buf, 256, stdin)) break;
		str = buf;
		while (*str && isspace(*str)) str++;
		while (*str && isspace(str[strlen(str) - 1])) str[strlen(str) - 1] = 0;
		if (!strcmp(str, "exit")) break;
		if (*str) p2p.PropagateMessage(str, strlen(str));
	}

	printf("Shutdown...\n");
	p2p.Shutdown();
	printf("Bye-bye!\n");

	return 0;
fail:
	printf("Can't perform operation '%s' with LMDB database, error %d.\n", mess, err);
	return err;
}
