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

static RBPtr make_rb(int epoch_num, uint8_t delegate_id) {
    RBPtr rb = std::make_shared<ApprovedRB>();
    rb->epoch_number = epoch_num;
    rb->primary_delegate = delegate_id;
    return rb;
}

static MBPtr make_mb(int epoch_num, uint8_t delegate_id, int sequence, const BlockHash &previous) {
    MBPtr mb = std::make_shared<ApprovedMB>();
    mb->epoch_number = epoch_num;
    mb->primary_delegate = delegate_id;
    mb->sequence = sequence;
    mb->previous = previous;
    return mb;
}

static EBPtr make_eb(int epoch_num, uint8_t delegate_id, const Tip &micro_tip) {
    EBPtr eb = std::make_shared<ApprovedEB>();
    eb->epoch_number = epoch_num;
    eb->primary_delegate = delegate_id;
    eb->micro_block_tip = micro_tip;
    extern Delegate init_delegate(AccountAddress account, Amount vote, Amount stake, bool starting_term);
    for (int i = 0; i < NUM_DELEGATES; i++)
    {
        eb->delegates[i] = init_delegate(0, 0, 0, 0);
    }
    return eb;
}

TEST (BlockCache, VerifyTest)
{
    system("rm -rf " TEST_DIR "; mkdir " TEST_DIR);

    bool error = false;
    boost::filesystem::path const data_path(TEST_DB);
    logos::block_store store(error, data_path);
    EXPECT_EQ(error, false);

    EBPtr e0 = make_eb(2, 0, Tip());
    MBPtr m0 = make_mb(3, 1, 0, BlockHash());

    Tip etip;
    etip.epoch = 2;
    etip.sqn = 0;
    etip.digest = e0->Hash();

    Tip mtip;
    mtip.epoch = 3;
    mtip.sqn = 0;
    mtip.digest = m0->Hash();

    {
        logos::transaction t(store.environment, nullptr, true);

        Tip etip;
        etip.epoch = 2;
        etip.sqn = 0;
        etip.digest = e0->Hash();

        store.epoch_put(*e0, t);
        store.epoch_tip_put(etip, t);
        store.micro_block_put(*m0, t);
        store.micro_block_tip_put(mtip, t);
    }

    logos::BlockWriteQueue q(store, true);
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
        MBPtr mb = make_mb(3, 8, 1, m0->Hash());
        EXPECT_EQ(q.VerifyAggSignature(mb), true);
        EXPECT_EQ(q.VerifyContent(mb, &s), true);
        std::cout << "MB status: " << ProcessResultToString(s.reason) << std::endl;
        EXPECT_EQ(q.BlockExists(mb), false);
        EXPECT_EQ(q.IsBlockCached(mb->Hash()), false);
    }
    {
        ValidationStatus s;
        s.progress = 0;
        EBPtr eb = make_eb(3, 9, mtip);
        EXPECT_EQ(q.VerifyAggSignature(eb), true);
        EXPECT_EQ(q.VerifyContent(eb, &s), true);
        std::cout << "EB status: " << ProcessResultToString(s.reason) << std::endl;
        EXPECT_EQ(q.BlockExists(eb), false);
        EXPECT_EQ(q.IsBlockCached(eb->Hash()), false);
    }
    std::cout << "VerifyTest end" << std::endl;
}
