#include <gtest/gtest.h>

#include <functional>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../consensus/persistence/block_cache.hpp"

#define TEST_DIR  ".logos_test"
#define TEST_DB   TEST_DIR "/data.ldb"

using RBPtr = logos::IBlockCache::RBPtr;
using MBPtr = logos::IBlockCache::MBPtr;
using EBPtr = logos::IBlockCache::EBPtr;

static RBPtr make_rb(int epoch_num, uint8_t delegate_id)
{
    RBPtr rb = std::make_shared<ApprovedRB>();
    rb->epoch_number = epoch_num;
    rb->primary_delegate = delegate_id;
    return rb;
}

static MBPtr make_mb(int epoch_num, uint8_t delegate_id, int sequence, const BlockHash &previous)
{
    MBPtr mb = std::make_shared<ApprovedMB>();
    mb->epoch_number = epoch_num;
    mb->primary_delegate = delegate_id;
    mb->sequence = sequence;
    mb->previous = previous;
    return mb;
}

static EBPtr make_eb(int epoch_num, uint8_t delegate_id, const Tip &micro_tip, const BlockHash &previous)
{
    EBPtr eb = std::make_shared<ApprovedEB>();
    eb->epoch_number = epoch_num;
    eb->primary_delegate = delegate_id;
    eb->micro_block_tip = micro_tip;
    eb->previous = previous;
    extern Delegate init_delegate(AccountAddress account, Amount vote, Amount stake, bool starting_term);
    for (int i = 0; i < NUM_DELEGATES; i++)
    {
        eb->delegates[i] = init_delegate(0, 0, 0, 0);
    }
    return eb;
}

struct test_data
{
    bool                    error;
    boost::filesystem::path data_path;
    logos::block_store      store;
    EBPtr                   e0;
    MBPtr                   m0;
    Tip                     etip;
    Tip                     mtip;
    std::queue<BlockHash>   store_q;

    test_data()
        : error(system("rm -rf " TEST_DIR "; mkdir " TEST_DIR))
        , data_path(TEST_DB)
        , store(error, data_path)
        , e0(make_eb(2, 0, Tip(), BlockHash()))
        , m0(make_mb(3, 1, 0, BlockHash()))
    {
        etip.epoch = 2;
        etip.sqn = 0;
        etip.digest = e0->Hash();

        mtip.epoch = 3;
        mtip.sqn = 0;
        mtip.digest = m0->Hash();

        {
            logos::transaction t(store.environment, nullptr, true);

            store.epoch_put(*e0, t);
            store.epoch_tip_put(etip, t);
            store.micro_block_put(*m0, t);
            store.micro_block_tip_put(mtip, t);
        }
    }
};

TEST (BlockCache, VerifyTest)
{
    test_data t;
    EXPECT_EQ(t.error, false);
    logos::BlockWriteQueue q(t.store, &t.store_q);

    {
        ValidationStatus s;
        s.progress = 0;
        RBPtr rb = make_rb(3, 7);
        EXPECT_EQ(q.VerifyAggSignature(rb), true);
        EXPECT_EQ(q.VerifyContent(rb, &s), true);
        std::cout << "RB status: " << ProcessResultToString(s.reason) << std::endl;
        EXPECT_EQ(q.BlockExists(rb), false);
        EXPECT_EQ(q.IsBlockCached(rb->Hash()), false);
    }
    {
        ValidationStatus s;
        s.progress = 0;
        MBPtr mb = make_mb(3, 8, 1, t.m0->Hash());
        EXPECT_EQ(q.VerifyAggSignature(mb), true);
        EXPECT_EQ(q.VerifyContent(mb, &s), true);
        std::cout << "MB status: " << ProcessResultToString(s.reason) << std::endl;
        EXPECT_EQ(q.BlockExists(mb), false);
        EXPECT_EQ(q.IsBlockCached(mb->Hash()), false);
    }
    {
        ValidationStatus s;
        s.progress = 0;
        EBPtr eb = make_eb(3, 9, t.mtip, t.e0->Hash());
        EXPECT_EQ(q.VerifyAggSignature(eb), true);
        EXPECT_EQ(q.VerifyContent(eb, &s), true);
        std::cout << "EB status: " << ProcessResultToString(s.reason) << std::endl;
        EXPECT_EQ(q.BlockExists(eb), false);
        EXPECT_EQ(q.IsBlockCached(eb->Hash()), false);
    }
    std::cout << "VerifyTest end" << std::endl;
}

TEST (BlockCache, WriteTest)
{
    test_data t;
    EXPECT_EQ(t.error, false);
    logos::BlockWriteQueue q(t.store, &t.store_q);
    std::vector<BlockHash> hashes;
    BlockHash hash;

    for (int i = 0; i < NUM_DELEGATES; ++i)
    {
        RBPtr rb = make_rb(3, i);
        hash = rb->Hash();
        EXPECT_EQ(q.BlockExists(rb), false);
        q.StoreBlock(rb);
        EXPECT_EQ(q.BlockExists(rb), true);
        hashes.push_back(hash);
    }

    MBPtr mb = make_mb(3, 9, 1, t.m0->Hash());
    hash = mb->Hash();
    EXPECT_EQ(q.BlockExists(mb), false);
    q.StoreBlock(mb);
    EXPECT_EQ(q.BlockExists(mb), true);
    hashes.push_back(hash);

    EBPtr eb = make_eb(3, 10, t.mtip, t.e0->Hash());
    hash = eb->Hash();
    EXPECT_EQ(q.BlockExists(eb), false);
    q.StoreBlock(eb);
    EXPECT_EQ(q.BlockExists(eb), true);
    hashes.push_back(hash);

    for (int i = 0; i < 3 && hashes.size() != t.store_q.size(); ++i)
    {
        sleep(1);
    }

    EXPECT_EQ(hashes.size(), t.store_q.size());

    for (int i = 0; i < hashes.size(); ++i)
    {
        EXPECT_EQ(hashes[i], t.store_q.front());
        EXPECT_EQ(q.IsBlockCached(hashes[i]), false);
        t.store_q.pop();
    }
}

#define N_BLOCKS NUM_DELEGATES

TEST (BlockCache, MicroBlocksLinearTest)
{
    test_data t;
    EXPECT_EQ(t.error, false);
    logos::BlockCache c(t.store, &t.store_q);
    std::vector<MBPtr> mbs;
    std::vector<BlockHash> hashes;
    BlockHash hash = t.m0->Hash();

    for (int i = 0; i < N_BLOCKS; ++i)
    {
        MBPtr mb = make_mb(3, i, i + 1, hash);
        hash = mb->Hash();
        hashes.push_back(hash);
        mbs.push_back(mb);
    }

    for (int i = 0; i < N_BLOCKS * N_BLOCKS; ++i)
    {
        int j = rand() % N_BLOCKS, k = rand() % N_BLOCKS;
        if (j != k)
        {
            MBPtr m = mbs[j];
            mbs[j] = mbs[k];
            mbs[k] = m;
        }
    }

    for (int i = 0; i < N_BLOCKS; ++i)
    {
        EXPECT_EQ(c.AddMicroBlock(mbs[i]), true);
    }

    for (int i = 0; i < 3 && hashes.size() != t.store_q.size(); ++i)
    {
        sleep(1);
    }

    EXPECT_EQ(hashes.size(), t.store_q.size());

    for (int i = 0; i < hashes.size(); ++i)
    {
        EXPECT_EQ(hashes[i], t.store_q.front());
        EXPECT_EQ(c.IsBlockCached(hashes[i]), false);
        t.store_q.pop();
    }
}
