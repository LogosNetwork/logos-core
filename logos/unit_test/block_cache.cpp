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

static RBPtr make_rb(int epoch_num, uint8_t delegate_id, int sequence, const BlockHash &previous)
{
    RBPtr rb = std::make_shared<ApprovedRB>();
    rb->epoch_number = epoch_num;
    rb->primary_delegate = delegate_id;
    rb->sequence = sequence;
    rb->previous = previous;
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
    logos::BlockWriteQueue q(t.store, 0, &t.store_q);

    {
        ValidationStatus s;
        s.progress = 0;
        RBPtr rb = make_rb(3, 7, 0, BlockHash());
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
    logos::BlockWriteQueue q(t.store, 0, &t.store_q);
    std::vector<BlockHash> hashes;
    BlockHash hash;

    for (int i = 0; i < NUM_DELEGATES; ++i)
    {
        RBPtr rb = make_rb(3, i, 0, BlockHash());
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

#undef N_BLOCKS
#define N_BLOCKS    8
#define N_DELEGATES 8
#define N_TOTAL     (N_BLOCKS * N_DELEGATES)

TEST (BlockCache, RequestsSquaredTest)
{
    test_data t;
    EXPECT_EQ(t.error, false);
    logos::BlockCache c(t.store, &t.store_q);
    std::vector<RBPtr> rbs;
    std::vector<BlockHash> hashes[N_DELEGATES];
    int indexes[N_DELEGATES] = {0};

    for (int i = 0; i < N_DELEGATES; ++i)
    {
        BlockHash hash;
        for (int j = 0; j < N_BLOCKS; ++j)
        {
            RBPtr rb = make_rb(3, (i * i) % (NUM_DELEGATES - 3), j, hash);
            hash = rb->Hash();
            hashes[i].push_back(hash);
            rbs.push_back(rb);
        }
    }

    for (int i = 0; i < N_TOTAL * N_TOTAL; ++i)
    {
        int j = rand() % N_TOTAL, k = rand() % N_TOTAL;
        if (j != k)
        {
            RBPtr r = rbs[j];
            rbs[j] = rbs[k];
            rbs[k] = r;
        }
    }

    for (int i = 0; i < N_TOTAL; ++i)
    {
        EXPECT_EQ(c.AddRequestBlock(rbs[i]), true);
    }

    for (int i = 0; i < 3 && t.store_q.size() != N_TOTAL; ++i)
    {
        sleep(1);
    }

    EXPECT_EQ(t.store_q.size(), N_TOTAL);

    for (int i = 0; i < N_TOTAL; ++i)
    {
        BlockHash hash = t.store_q.front();
        int j;
        for (j = 0; j < N_DELEGATES; ++j)
        {
            if (indexes[j] < N_BLOCKS && hash == hashes[j][indexes[j]])
            {
                indexes[j]++;
                break;
            }
        }
        EXPECT_NE(j, N_DELEGATES);
        EXPECT_EQ(c.IsBlockCached(hash), false);
        t.store_q.pop();
    }
}

#undef N_BLOCKS
#undef N_DELEGATES
#undef N_TOTAL
#define N_RBLOCKS 4
#define N_MBLOCKS 2
#define N_DELEGATES 4
#define N_EPOCHS 2

TEST (BlockCache, MixedBlocksTest)
{
    test_data t;
    EXPECT_EQ(t.error, false);
    logos::BlockCache c(t.store, &t.store_q);
    std::vector<RBPtr> rbs;
    std::vector<MBPtr> mbs;
    std::vector<EBPtr> ebs;
    BlockHash ehash = t.e0->Hash(), mhash = t.m0->Hash(), rhash;
    std::vector<int> indexes;
    int mb_sqn = 0;

    for (int i = 0; i < N_EPOCHS; ++i)
    {
        BlockHash rhashes[N_DELEGATES];
        for (int j = 0; j < N_RBLOCKS; ++j)
        {
            for (int k = 0; k < N_DELEGATES; ++k)
            {
                RBPtr rb = make_rb(3 + i, k * (i + 1), j, rhashes[k]);
                rhash = rb->Hash();
                rhashes[k] = rhash;
                indexes.push_back(rbs.size() << 2);
                rbs.push_back(rb);
            }
            if ((j + 1) % (N_RBLOCKS / N_MBLOCKS) == 0)
            {
                MBPtr mb = make_mb(3 + i, N_DELEGATES * (i + 1), ++mb_sqn, mhash);
                for (int k = 0; k < N_DELEGATES; ++k)
                {
                    mb->tips[k * (i + 1)].digest = rhashes[k];
                }
                if (j == N_RBLOCKS - 1)
                    mb->last_micro_block = true;
                mhash = mb->Hash();
                indexes.push_back(mbs.size() << 2 | 1);
                mbs.push_back(mb);
            }
        }
        Tip mtip;
        mtip.epoch = 3 + i;
        mtip.sqn = mb_sqn;
        mtip.digest = mhash;
        EBPtr eb = make_eb(3 + i, 30 + i, mtip, ehash);
        ehash = eb->Hash();
        indexes.push_back(ebs.size() << 2 | 2);
        ebs.push_back(eb);
    }

    int size = indexes.size();

    for (int i = 0; i < size * size; ++i)
    {
        int j = rand() % size, k = rand() % size;
        if (j != k)
        {
            int ind = indexes[j];
            indexes[j] = indexes[k];
            indexes[k] = ind;
        }
    }

    for (int i = 0; i < size; ++i)
    {
        int ind = indexes[i];
        switch(ind & 3)
        {
        case 0:
            EXPECT_EQ(c.AddRequestBlock(rbs[ind >> 2]), true);
            break;
        case 1:
            EXPECT_EQ(c.AddMicroBlock(mbs[ind >> 2]), true);
            break;
        case 2:
            EXPECT_EQ(c.AddEpochBlock(ebs[ind >> 2]), true);
            break;
        default:
            EXPECT_EQ(0,1);
            break;
        }
    }

    for (int i = 0; i < 3 && t.store_q.size() != size; ++i)
    {
        sleep(1);
    }

    EXPECT_EQ(t.store_q.size(), size);

    int rindexes[N_EPOCHS * N_DELEGATES] = {0}, mindexes[N_EPOCHS] = {0}, eindex = 0;

    for (int i = 0; i < size; ++i)
    {
        BlockHash hash = t.store_q.front();
        t.store_q.pop();
        EXPECT_EQ(c.IsBlockCached(hash), false);
        int j;

        for (j = 0; j < N_EPOCHS * N_DELEGATES; ++j)
        {
            if (rindexes[j] < N_RBLOCKS && hash == rbs[j * N_RBLOCKS + rindexes[j]]->Hash())
            {
                rindexes[j]++;
                break;
            }
        }
        if (j < N_EPOCHS * N_DELEGATES)
            continue;

        for (j = 0; j < N_EPOCHS; ++j)
        {
            if (mindexes[j] < N_MBLOCKS && hash == mbs[j * N_MBLOCKS + mindexes[j]]->Hash())
            {
                mindexes[j]++;
                break;
            }
        }
        if (j < N_EPOCHS)
            continue;

        if (eindex < N_EPOCHS && hash == ebs[eindex]->Hash())
        {
            eindex++;
        }
        else
        {
            EXPECT_EQ(2,3);
        }
    }

}
