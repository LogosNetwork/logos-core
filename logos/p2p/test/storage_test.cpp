#include <gtest/gtest.h>

#include <functional>
#include <cstdint>
#include <cstdio>
#include "../p2p.h"

TEST (StorageTest, VerifyBanInterface)
{
    char *argv[] = {(char *)"p2p", 0};
    p2p_interface p2p;
    p2p_config config;

    config.argc = 1;
    config.argv = argv;
    config.boost_io_service = 0;
    config.test_mode = true;

    config.scheduleAfterMs = [](std::function<void()> const &handler, unsigned ms)
    {
        printf("scheduleAfterMs called.\n");
    };

    config.userInterfaceMessage = [](int type, const char *mess)
    {
        printf("%s%s: %s\n", (type & P2P_UI_INIT ? "init " : ""),
            (type & P2P_UI_ERROR ? "error" : type & P2P_UI_WARNING ? "warning" : "message"), mess);
    };

/*
    EXPECT_EQ(mdb_env_create(&config.lmdb_env), 0;
    EXPECT_EQ(mdb_env_set_maxdbs(config.lmdb_env, 1), 0);
    EXPECT_EQ(mdb_env_open(config.lmdb_env, ".", 0, 0644), 0;
    EXPECT_EQ(mdb_txn_begin(config.lmdb_env, 0, 0, &txn), 0;
    EXPECT_EQ(mdb_dbi_open(txn, "p2p_db", MDB_CREATE, &config.lmdb_dbi), 0);
    EXPECT_EQ(mdb_txn_commit(txn), 0);
*/
    p2p.Init(config);

    p2p.add_to_blacklist("8.8.8.8");
    p2p.add_to_blacklist("230.1.0.129");

    EXPECT_EQ(p2p.is_blacklisted("4.4.4.4"), false);
    EXPECT_EQ(p2p.is_blacklisted("8.8.8.8"), true);
    EXPECT_EQ(p2p.is_blacklisted("8.8.8.9"), false);
    EXPECT_EQ(p2p.is_blacklisted("230.0.0.129"), false);
    EXPECT_EQ(p2p.is_blacklisted("230.1.0.129"), true);
}

