#include <gtest/gtest.h>
#include <iostream>
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

static RBPtr make_rb(int epoch_num, uint8_t delegate_id, int sequence, const BlockHash &previous,
                     const std::vector<BlockHash> &requests_previous = std::vector<BlockHash>(),
                     const std::vector<AccountAddress> &requests_source = std::vector<AccountAddress>(),
                     const std::vector<Amount> &fees = std::vector<Amount>())
{
    RBPtr rb = std::make_shared<ApprovedRB>();
    rb->epoch_number = epoch_num;
    rb->primary_delegate = delegate_id;
    rb->sequence = sequence;
    rb->previous = previous;
    for (int i = 0; i < requests_previous.size(); ++i)
    {
        auto r = std::make_shared<Request>();
        r->previous = requests_previous[i];
        if (i < requests_source.size())
            r->origin = requests_source[i];
        if (i < fees.size())
            r->fee = fees[i];
        rb->requests.push_back(r);
    }
    return rb;
}

static MBPtr make_mb(int epoch_num, uint8_t delegate_id, int sequence, const BlockHash &previous, bool last = false)
{
    MBPtr mb = std::make_shared<ApprovedMB>();
    mb->epoch_number = epoch_num;
    mb->primary_delegate = delegate_id;
    mb->sequence = sequence;
    mb->previous = previous;
    mb->last_micro_block = last;
    return mb;
}

static EBPtr make_eb(int epoch_num, uint8_t delegate_id, const Tip &micro_tip, const BlockHash &previous, uint64_t total_RBs)
{
    EBPtr eb = std::make_shared<ApprovedEB>();
    eb->epoch_number = epoch_num;
    eb->primary_delegate = delegate_id;
    eb->micro_block_tip = micro_tip;
    eb->previous = previous;
    eb->total_RBs = total_RBs;
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
    //MBPtr                   m1;
    Tip                     etip;
    Tip                     mtip;
    std::queue<BlockHash>   store_q;

    test_data(bool last = false)
        : error(system("rm -rf " TEST_DIR "; mkdir " TEST_DIR))
        , data_path(TEST_DB)
        , store(error, data_path)
        , e0(make_eb(2, 0, Tip(), BlockHash(), 0))
        , m0(make_mb(3, 1, 0, BlockHash(), last))
        //, m1(make_mb(4, 2, 0, BlockHash()))
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
            //store.micro_block_put(*m1, t);
            store.micro_block_tip_put(mtip, t);
        }
    }
};

TEST (BlockCache, VerifyTest)
{
    test_data t(true);
    EXPECT_EQ(t.error, false);
    boost::asio::io_service service;
    logos::BlockWriteQueue q(service, t.store, 0, &t.store_q);

    {
        ValidationStatus s;
        s.progress = 0;
        RBPtr rb = make_rb(3, 7, 0, BlockHash());
        EXPECT_EQ(q.VerifyAggSignature(rb), true);
        EXPECT_EQ(q.VerifyContent(rb, &s), true);
        std::cout << "RB status: " << ProcessResultToString(s.reason) << std::endl;
        EXPECT_EQ(q.BlockExists(rb), false);
        EXPECT_EQ(q.IsBlockQueued(rb->Hash()), false);
    }
    {
        ValidationStatus s;
        s.progress = 0;
        MBPtr mb = make_mb(3, 8, 1, t.m0->Hash());
        EXPECT_EQ(q.VerifyAggSignature(mb), true);
        EXPECT_EQ(q.VerifyContent(mb, &s), true);
        std::cout << "MB status: " << ProcessResultToString(s.reason) << std::endl;
        EXPECT_EQ(q.BlockExists(mb), false);
        EXPECT_EQ(q.IsBlockQueued(mb->Hash()), false);
    }
    {
        ValidationStatus s;
        s.progress = 0;
        EBPtr eb = make_eb(3, 9, t.mtip, t.e0->Hash(), 0);
        EXPECT_EQ(q.VerifyAggSignature(eb), true);
        EXPECT_EQ(q.VerifyContent(eb, &s), true);
        std::cout << "EB status: " << ProcessResultToString(s.reason) << std::endl;
        EXPECT_EQ(q.BlockExists(eb), false);
        EXPECT_EQ(q.IsBlockQueued(eb->Hash()), false);
    }
    std::cout << "VerifyTest end" << std::endl;
}

TEST (BlockCache, WriteTest)
{
    test_data t;
    EXPECT_EQ(t.error, false);
    boost::asio::io_service service;
    logos::BlockWriteQueue q(service, t.store, 0, &t.store_q);
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

    MBPtr mb = make_mb(3, 9, 1, t.m0->Hash(), true);
    hash = mb->Hash();
    EXPECT_EQ(q.BlockExists(mb), false);
    q.StoreBlock(mb);
    EXPECT_EQ(q.BlockExists(mb), true);
    hashes.push_back(hash);

    EBPtr eb = make_eb(3, 10, t.mtip, t.e0->Hash(), NUM_DELEGATES);
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
        EXPECT_EQ(q.IsBlockQueued(hashes[i]), false);
        t.store_q.pop();
    }
}

#define N_BLOCKS NUM_DELEGATES

TEST (BlockCache, MicroBlocksLinearTest)
{
    test_data t;
    EXPECT_EQ(t.error, false);
    boost::asio::io_service service;
    logos::BlockCache c(service, t.store, &t.store_q);
    std::vector<MBPtr> mbs;
    std::vector<BlockHash> hashes;
    BlockHash hash = t.m0->Hash();

    for (int i = 0; i < N_BLOCKS; ++i)
    {
        MBPtr mb = make_mb(3, i, i + 1, hash, i == N_BLOCKS - 1);
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
        EXPECT_EQ(c.AddMicroBlock(mbs[i]), logos::IBlockCache::add_result::OK);
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
    boost::asio::io_service service;
    logos::BlockCache c(service, t.store, &t.store_q);
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
        EXPECT_EQ(c.AddRequestBlock(rbs[i]), logos::IBlockCache::add_result::OK);
    }

    for (int i = 0; i < 10 && t.store_q.size() != N_TOTAL; ++i)
    {
        sleep(1);
    }

    EXPECT_EQ(N_TOTAL, t.store_q.size());

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
        EXPECT_FALSE(c.IsBlockCached(hash));
        t.store_q.pop();
    }
}

#undef N_BLOCKS
#undef N_DELEGATES
#undef N_TOTAL
#define N_RBLOCKS 3
#define N_MBLOCKS 2
#define N_DELEGATES 2
#define N_EPOCHS 2


TEST (BlockCache, MixedBlocksTest)
{
    test_data t;
    EXPECT_EQ(t.error, false);
    boost::asio::io_service service;
    logos::BlockCache c(service, t.store, &t.store_q);
    std::vector<RBPtr> rbs, rbs0;
    std::vector<MBPtr> mbs;
    std::vector<EBPtr> ebs;
    BlockHash ehash = t.e0->Hash(), mhash = t.m0->Hash(), rhash;
    std::vector<int> indexes;
    int mb_sqn = 0;//, blocks_number = NUM_DELEGATES + N_DELEGATES * (N_RBLOCKS / N_MBLOCKS);

    uint64_t rb_count = 0;
    uint64_t prev_mb_rb_count = 0;
    for (int i = 0; i < N_EPOCHS; ++i)
    {
        BlockHash rhashes[NUM_DELEGATES];
        for (int j = 0; j < NUM_DELEGATES; ++j)
        {
            RBPtr rb = make_rb(3 + i, j, 0, BlockHash());
            ++rb_count;
            rhashes[j] = rb->Hash();
            c.StoreRequestBlock(rb);
            rbs0.push_back(rb);
        }
        for (int j = 0; j < N_RBLOCKS; ++j)
        {
            for (int k = 0; k < N_DELEGATES; ++k)
            {
                RBPtr rb = make_rb(3 + i, k * (i + 1), j + 1, rhashes[k * (i + 1)]);
                ++rb_count;
                rhash = rb->Hash();
                rhashes[k * (i + 1)] = rhash;
                indexes.push_back(rbs.size() << 2);
                rbs.push_back(rb);
            }
            if ((j + 1) % (N_RBLOCKS / N_MBLOCKS) == 0)
            {
                MBPtr mb = make_mb(3 + i, N_DELEGATES * (i + 1), ++mb_sqn, mhash, j == N_RBLOCKS - 1);
                for (int k = 0; k < NUM_DELEGATES; ++k)
                {
                    mb->tips[k].epoch = 3 + i;
                    mb->tips[k].sqn = (k % (i + 1) || k / (i + 1) >= N_DELEGATES ? 0 : j + 1);
                    mb->tips[k].digest = rhashes[k];
                }
                mb->number_batch_blocks = rb_count - prev_mb_rb_count;//blocks_number;
                prev_mb_rb_count = rb_count;
                if (j == N_RBLOCKS - 1)
                    mb->last_micro_block = true;
                mhash = mb->Hash();
                indexes.push_back(mbs.size() << 2 | 1);
                mbs.push_back(mb);
                //blocks_number = N_DELEGATES * (N_RBLOCKS / N_MBLOCKS);
            }
        }
        Tip mtip;
        mtip.epoch = 3 + i;
        mtip.sqn = mb_sqn;
        mtip.digest = mhash;
        EBPtr eb = make_eb(3 + i, 30 + i, mtip, ehash, rb_count);
        ehash = eb->Hash();
        indexes.push_back(ebs.size() << 2 | 2);
        ebs.push_back(eb);

        //mhash = t.m1->Hash();
        //mb_sqn = 0;
        //blocks_number = 2 * NUM_DELEGATES + N_DELEGATES * (N_RBLOCKS + N_RBLOCKS / N_MBLOCKS);
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
            EXPECT_EQ(c.AddRequestBlock(rbs[ind >> 2]), logos::IBlockCache::add_result::OK);
            break;
        case 1:
            EXPECT_EQ(c.AddMicroBlock(mbs[ind >> 2]), logos::IBlockCache::add_result::OK);
            break;
        case 2:
            EXPECT_EQ(c.AddEpochBlock(ebs[ind >> 2]), logos::IBlockCache::add_result::OK);
            break;
        default:
            EXPECT_EQ(0,1);
            break;
        }
    }

    for (int i = 0; i < 10 && t.store_q.size() != size + N_EPOCHS * NUM_DELEGATES; ++i)
    {
        std::cout << "t.store_q.size()=" << t.store_q.size()
                  << ", size + N_EPOCHS * NUM_DELEGATES=" << size + N_EPOCHS * NUM_DELEGATES
                  << std::endl;
        sleep(1);
    }

    if(t.store_q.size() != size + N_EPOCHS * NUM_DELEGATES)
    {
        c.IsBlockCached(0);
        exit(-1);
    }

    EXPECT_EQ(size + N_EPOCHS * NUM_DELEGATES, t.store_q.size());

    for (int i = 0; i < N_EPOCHS * NUM_DELEGATES; ++i)
    {
        BlockHash hash = t.store_q.front();
        t.store_q.pop();
        EXPECT_EQ(c.IsBlockCached(hash), false);
        EXPECT_EQ(hash, rbs0[i]->Hash());
    }

    int rindexes[N_EPOCHS][N_DELEGATES] = {0}, mindexes[N_EPOCHS] = {0}, eindex = 0;

    for (int i = 0; i < size; ++i)
    {
        BlockHash hash = t.store_q.front();
        t.store_q.pop();
        EXPECT_FALSE(c.IsBlockCached(hash));

//        for (int j = 0; j < N_EPOCHS; ++j)
//        {
//            for (int k = 0; k < N_DELEGATES; ++k)
//            {
//                if (rindexes[j][k] < N_RBLOCKS && hash == rbs[(j * N_RBLOCKS + rindexes[j][k]) * N_DELEGATES + k]->Hash())
//                {
//                    rindexes[j][k]++;
//                    printf("rindex[%d][%d] = %d\n", j, k, rindexes[j][k]);
//                    goto cont;
//                }
//            }
//
//            if (mindexes[j] < N_MBLOCKS && hash == mbs[j * N_MBLOCKS + mindexes[j]]->Hash())
//            {
//                mindexes[j]++;
//                printf("mindex[%d] = %d\n", j, mindexes[j]);
//                goto cont;
//            }
//        }
//
//        if (eindex < N_EPOCHS && hash == ebs[eindex]->Hash())
//        {
//            eindex++;
//            printf("eindex = %d\n", eindex);
//        }
//        else
//        {
//            EXPECT_EQ(2,3);
//        }
//    cont:;
    }

}

TEST (BlockCache, HashDependenciesTest)
{
    test_data t;
    EXPECT_EQ(t.error, false);
    boost::asio::io_service service;
    logos::BlockCache c(service, t.store, &t.store_q);
    RBPtr rb[4];
    std::vector<BlockHash> v[4], h;

    v[0].push_back(BlockHash());
    rb[0] = make_rb(3, 0, 0, BlockHash(), v[0]);
    h.push_back(rb[0]->Hash());

    v[1].push_back(rb[0]->requests[0]->Hash());
    rb[1] = make_rb(3, 0, 1, h[0], v[1]);
    h.push_back(rb[1]->Hash());

    v[2].push_back(rb[1]->requests[0]->Hash());
    rb[2] = make_rb(3, 1, 0, BlockHash(), v[2]);
    h.push_back(rb[2]->Hash());

    v[3].push_back(rb[2]->requests[0]->Hash());
    rb[3] = make_rb(3, 1, 1, h[2], v[3]);
    h.push_back(rb[3]->Hash());

    for (int i = 0; i < 4; ++i)
    {
        c.AddRequestBlock(rb[3 - i]);
        h[3 - i] = rb[3 - i]->Hash();
    }

    for (int i = 0; i < 2 && t.store_q.size() != 4; ++i)
    {
        sleep(1);
    }

    EXPECT_EQ(t.store_q.size(), 4);

    for (int i = 0; i < 4; ++i)
    {
        BlockHash hash = t.store_q.front();
        t.store_q.pop();
        EXPECT_EQ(c.IsBlockCached(hash), false);
        EXPECT_EQ(hash, h[i]);
    }
}

TEST (BlockCache, AccountDependenciesTest)
{
    test_data t;
    EXPECT_EQ(t.error, false);
    boost::asio::io_service service;
    logos::BlockCache c(service, t.store, &t.store_q);
    RBPtr rb[4];
    std::vector<BlockHash> v[4], h;
    std::vector<AccountAddress> a[4];
    std::vector<Amount> f[4];

    BlockHash G = t.m0->Hash(), H = t.e0->Hash();
    AccountAddress A, B;
    memcpy(&A, &G, sizeof(A));
    memcpy(&B, &H, sizeof(B));

    v[0].push_back(BlockHash());
    a[0].push_back(A);
    f[0].push_back(1);
    rb[0] = make_rb(3, 0, 0, BlockHash(), v[0], a[0], f[0]);
    h.push_back(rb[0]->Hash());

    v[1].push_back(rb[0]->requests[0]->Hash());
    a[1].push_back(B);
    f[1].push_back(1);
    rb[1] = make_rb(3, 0, 1, h[0], v[1], a[1], f[1]);
    h.push_back(rb[1]->Hash());

    v[2].push_back(BlockHash());
    a[2].push_back(B);
    f[2].push_back(0);
    rb[2] = make_rb(3, 1, 0, BlockHash(), v[2], a[2], f[2]);
    h.push_back(rb[2]->Hash());

    v[3].push_back(rb[2]->requests[0]->Hash());
    a[3].push_back(A);
    f[3].push_back(0);
    rb[3] = make_rb(3, 1, 1, h[2], v[3], a[3], f[3]);
    h.push_back(rb[3]->Hash());

    for (int i = 0; i < 4; ++i)
    {
        c.AddRequestBlock(rb[3 - i]);
        h[3 - i] = rb[3 - i]->Hash();
    }

    for (int i = 0; i < 2 && t.store_q.size() != 4; ++i)
    {
        sleep(1);
    }

    EXPECT_EQ(t.store_q.size(), 4);

    for (int i = 0; i < 4; ++i)
    {
        BlockHash hash = t.store_q.front();
        t.store_q.pop();
        EXPECT_EQ(c.IsBlockCached(hash), false);
        EXPECT_EQ(hash, h[i]);
    }
}
