#include <gtest/gtest.h>

#include <functional>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "../../../lmdb/libraries/liblmdb/lmdb.h"
#include "../p2p.h"

#define TEST_DIR    ".logos_test"

TEST (StorageTest, VerifyBanInterface)
{
    char *argv[] = {(char *)"unit_test", (char *)"-debug=net", 0};
    p2p_config config;

    config.argc = 2;
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

    system("rm -rf " TEST_DIR "; mkdir " TEST_DIR);

    for (int i = 0; i < 2; ++i)
    {
        p2p_interface p2p;
        MDB_txn *txn;

        EXPECT_EQ(mdb_env_create(&config.lmdb_env), 0);
        EXPECT_EQ(mdb_env_set_maxdbs(config.lmdb_env, 1), 0);
        EXPECT_EQ(mdb_env_open(config.lmdb_env, TEST_DIR, 0, 0644), 0);
        EXPECT_EQ(mdb_txn_begin(config.lmdb_env, 0, 0, &txn), 0);
        EXPECT_EQ(mdb_dbi_open(txn, "p2p_db", MDB_CREATE, &config.lmdb_dbi), 0);
        EXPECT_EQ(mdb_txn_commit(txn), 0);

        EXPECT_EQ(p2p.Init(config), true);

        if (i == 0)
        {
            EXPECT_EQ(p2p.load_databases(), false);
            p2p.add_to_blacklist("8.8.8.8");
            p2p.add_to_blacklist("230.1.0.129");
        }
        else
        {
            EXPECT_EQ(p2p.load_databases(), true);
        }

        EXPECT_EQ(p2p.is_blacklisted("4.4.4.4"), false);
        EXPECT_EQ(p2p.is_blacklisted("8.8.8.8"), true);
        EXPECT_EQ(p2p.is_blacklisted("8.8.8.9"), false);
        EXPECT_EQ(p2p.is_blacklisted("8.128.8.8"), false);
        EXPECT_EQ(p2p.is_blacklisted("230.0.0.129"), false);
        EXPECT_EQ(p2p.is_blacklisted("230.1.0.129"), true);
        EXPECT_EQ(p2p.is_blacklisted("255.1.0.129"), false);

        EXPECT_EQ(p2p.save_databases(), true);

        mdb_dbi_close(config.lmdb_env, config.lmdb_dbi);
        EXPECT_EQ(mdb_env_sync(config.lmdb_env, 1), 0);
        mdb_env_close(config.lmdb_env);
    }
}

