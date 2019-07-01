#include <gtest/gtest.h>

#include <functional>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../consensus/persistence/block_cache.hpp"

#define TEST_DIR  ".logos_test"
#define TEST_DB   TEST_DIR "/data.ldb"

TEST (BlockCache, SimpleTest)
{
    system("rm -rf " TEST_DIR "; mkdir " TEST_DIR);

    bool error = false;
    boost::filesystem::path const data_path(TEST_DB);
    logos::block_store store(error, data_path);
    logos::BlockCache block_cache(store, true);
    EXPECT_EQ(error, false);
}
