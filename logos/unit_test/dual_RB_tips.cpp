#include <gtest/gtest.h>
#include <logos/blockstore.hpp>
#include <logos/blockstore.hpp>
#include <logos/consensus/persistence/epoch/epoch_persistence.hpp>
#include <logos/consensus/persistence/request/request_persistence.hpp>
#include <logos/consensus/persistence/reservations.hpp>
#include <logos/unit_test/msg_validator_setup.hpp>
#include <random>

extern PrePrepareMessage<ConsensusType::Epoch> create_eb_preprepare();

logos::block_store * get_and_setup_db()
{
    logos::block_store* store(get_db());
    store->clear(store->receive_db);
    store->clear(store->request_tips_db);
    store->clear(store->micro_block_db);
    store->clear(store->micro_block_tip_db);
    store->clear(store->epoch_db);
    store->clear(store->epoch_tip_db);

    return store;
}

bool are_rbs_equal(const ApprovedRB & rb1, const ApprovedRB & rb2)
{
    return rb1.version == rb2.version &&
        rb1.type == rb2.type &&
        rb1.consensus_type == rb2.consensus_type &&
        rb1.mpf == rb2.mpf &&
        rb1.payload_size == rb2.payload_size &&
        rb1.primary_delegate == rb2.primary_delegate &&
        rb1.epoch_number == rb2.epoch_number &&
        rb1.sequence == rb2.sequence &&
        rb1.timestamp == rb2.timestamp &&
        rb1.previous == rb2.previous &&
        rb1.requests == rb2.requests &&
        rb1.hashes == rb2.hashes &&
        rb1.post_prepare_sig == rb2.post_prepare_sig &&
        rb1.post_commit_sig == rb2.post_commit_sig &&
        rb1.next == rb2.next;
}

void populate_random_data(BlockHash & hash)
{
    logos::random_pool.GenerateBlock (hash.bytes.data (), hash.bytes.size ());
}

// Test if `request_block_update_prev` is working
TEST (DualRBTip, UpdateFirstPrev)
{
    logos::block_store *store(get_and_setup_db());
    logos::transaction txn(store->environment, nullptr, true);

    // create request block
    ApprovedRB block1;

    // write to db
    bool res (store->request_block_put(block1, txn));
    ASSERT_FALSE(res);

    ApprovedRB block2;
    res = store->request_block_get(block1.Hash(), block2, txn);
    ASSERT_FALSE(res);

    ASSERT_TRUE(are_rbs_equal(block1, block2));


    // update block prev field in db
    BlockHash prev_hash;
    populate_random_data(prev_hash);

    res = store->request_block_update_prev(block1.Hash(), prev_hash, txn);
    ASSERT_FALSE(res);

    // read into new block
    ApprovedRB block3;
    res = store->request_block_get(block1.Hash(), block3, txn);
    ASSERT_FALSE(res);

    // compare
    auto old_hash (block1.Hash());
    block1.previous = prev_hash;
    auto new_hash (block1.Hash());

    ASSERT_EQ(old_hash, new_hash);
    ASSERT_TRUE(are_rbs_equal(block1, block3));

}

// Test fetching epoch's first request blocks (PersistenceManager<ECT>::GetEpochFirstRBs)
TEST (DualRBTip, EpochFirstRBs)
{
    logos::block_store *store(get_and_setup_db());

    uint32_t cur_epoch (5);
    uint8_t delegate_with_tip (2);
    Tip tip_0;

    {
        // create one none empty tip and store in db
        logos::transaction txn(store->environment, nullptr, true);

        ApprovedRB block0;
        block0.primary_delegate = delegate_with_tip;
        block0.epoch_number = cur_epoch;
        ASSERT_FALSE(store->request_block_put(block0, txn));

        tip_0 = block0.CreateTip();
        ASSERT_FALSE(store->request_tip_put(delegate_with_tip, cur_epoch, tip_0, txn));

        // create another one for the same delegate
        ApprovedRB block1;
        block1.primary_delegate = delegate_with_tip;
        block1.epoch_number = cur_epoch;
        block1.previous = tip_0.digest;
        block1.sequence = 1;
        ASSERT_FALSE(store->request_block_put(block1, txn));
        ASSERT_FALSE(store->request_tip_put(delegate_with_tip, cur_epoch, block1.CreateTip(), txn));
    }


    // try to retrieve all tips
    BatchTips cur_e_first;
    store->GetEpochFirstRBs(cur_epoch, cur_e_first);

    // verify correct content
    for (uint8_t delegate = 0; delegate < NUM_DELEGATES; delegate++)
    {
        if (delegate == delegate_with_tip)
        {
            ASSERT_EQ(cur_e_first[delegate], tip_0);
        }
        else
        {
            ASSERT_TRUE(cur_e_first[delegate].digest.is_zero());
        }
    }
}

// Test linking through epoch (PersistenceManager<ECT>::LinkAndUpdateTips), current epoch has no request tip
TEST (DualRBTip, EpochLinking1)
{
    logos::block_store *store(get_and_setup_db());

    uint32_t cur_epoch (10);
    uint8_t delegate (15);
    BlockHash prev_hash;

    {
        // store prev epoch's tip block and hash
        logos::transaction txn(store->environment, nullptr, true);

        ApprovedRB prev_block;
        prev_block.primary_delegate = delegate;
        prev_block.epoch_number = cur_epoch - 1;
        prev_hash = prev_block.Hash();

        ASSERT_FALSE(store->request_block_put(prev_block, txn));
        ASSERT_FALSE(store->request_tip_put(delegate, cur_epoch - 1, prev_block.CreateTip(), txn));
    }

    std::shared_ptr<Reservations> reservations (std::make_shared<ConsensusReservations>(*store));
    PersistenceManager<ConsensusType::Epoch> epoch_persistence (*store, reservations);

    {
        Tip nonexistent_tip;
        ASSERT_TRUE(nonexistent_tip.digest.is_zero());
        logos::transaction txn(store->environment, nullptr, true);
        epoch_persistence.LinkAndUpdateTips(delegate, cur_epoch, nonexistent_tip, txn);
    }

    // request block tip should have been rolled over to current epoch
    Tip tip;
    ASSERT_TRUE(store->request_tip_get(delegate, cur_epoch - 1, tip));
    ASSERT_FALSE(store->request_tip_get(delegate, cur_epoch, tip));
    ASSERT_EQ(tip.digest, prev_hash);
}

// Test linking through epoch (PersistenceManager<ECT>::LinkAndUpdateTips), current epoch has request tip
TEST (DualRBTip, EpochLinking2)
{
    logos::block_store *store(get_and_setup_db());

    uint32_t cur_epoch (10);
    uint8_t delegate (15);
    BlockHash prev_hash, cur_hash;
    Tip cur_tip;
    {
        // store prev + current epochs' tip blocks and hashes
        logos::transaction txn(store->environment, nullptr, true);

        ApprovedRB prev_block, cur_block;
        prev_block.primary_delegate = delegate;
        prev_block.epoch_number = cur_epoch - 1;
        prev_hash = prev_block.Hash();

        cur_block.primary_delegate = delegate;
        cur_block.epoch_number = cur_epoch;
        cur_hash = cur_block.Hash();
        cur_tip = cur_block.CreateTip();

        ASSERT_FALSE(store->request_block_put(prev_block, txn));
        ASSERT_FALSE(store->request_tip_put(delegate, cur_epoch - 1, prev_block.CreateTip(), txn));
        ASSERT_FALSE(store->request_block_put(cur_block, txn));
        ASSERT_FALSE(store->request_tip_put(delegate, cur_epoch, cur_block.CreateTip(), txn));
    }

    std::shared_ptr<Reservations> reservations (std::make_shared<ConsensusReservations>(*store));
    PersistenceManager<ConsensusType::Epoch> epoch_persistence (*store, reservations);

    {
        logos::transaction txn(store->environment, nullptr, true);
        epoch_persistence.LinkAndUpdateTips(delegate, cur_epoch, cur_tip, txn);
    }

    // request block tip should have been updated to current block's tip
    Tip tip;
    ASSERT_TRUE(store->request_tip_get(delegate, cur_epoch - 1, tip));
    ASSERT_FALSE(store->request_tip_get(delegate, cur_epoch, tip));
    ASSERT_EQ(tip.digest, cur_hash);

    ApprovedRB prev_block, cur_block;
    ASSERT_FALSE(store->request_block_get(prev_hash, prev_block));
    ASSERT_FALSE(store->request_block_get(cur_hash, cur_block));

    // previous epoch's last request block and cur epoch's first request block should point to each other
    ASSERT_EQ(prev_block.next, cur_hash);
    ASSERT_EQ(cur_block.previous, prev_hash);
}

// Test linking through request block (PersistenceManager<R>::StoreRequestBlock), before epoch block proposal
TEST (DualRBTip, RequestBlockLinking1)
{
    logos::block_store *store(get_and_setup_db());

    // Params setup
    uint32_t cur_epoch (10);
    uint8_t delegate (15);

    // Store epoch block twice previous, epoch tip, prev epoch's last request block
    auto block = create_eb_preprepare();
    AggSignature sig;
    ApprovedEB preprev_e(block, sig, sig);
    ApprovedRB prev_r;

    preprev_e.epoch_number = cur_epoch - 2;
    prev_r.epoch_number = cur_epoch - 1;
    {
        logos::transaction txn(store->environment, nullptr, true);

        ASSERT_FALSE(store->epoch_put(preprev_e, txn));
        ASSERT_FALSE(store->epoch_tip_put(preprev_e.CreateTip(), txn));
        ASSERT_FALSE(store->request_block_put(prev_r, txn));
        ASSERT_FALSE(store->request_tip_put(delegate, prev_r.epoch_number, prev_r.CreateTip(), txn));
    }

    ApprovedRB cur_r;
    cur_r.epoch_number = cur_epoch;

    std::shared_ptr<Reservations> reservations (std::make_shared<ConsensusReservations>(*store));
    PersistenceManager<ConsensusType::Request> request_persistence (*store, reservations);

    // Request persistence StoreRequestBlock should do no linking
    {
        logos::transaction txn(store->environment, nullptr, true);
        request_persistence.StoreRequestBlock(cur_r, txn, delegate);
    }

    // check that hashes match
    Tip db_prev_r_tip, db_cur_r_tip;
    BlockHash & db_prev_r_hash = db_prev_r_tip.digest;
    BlockHash & db_cur_r_hash = db_cur_r_tip.digest;
    ASSERT_FALSE(store->request_tip_get(delegate, cur_epoch - 1, db_prev_r_tip));
    ASSERT_EQ(db_prev_r_hash, prev_r.Hash());
    ASSERT_FALSE(store->request_tip_get(delegate, cur_epoch, db_cur_r_tip));
    ASSERT_EQ(db_cur_r_hash, cur_r.Hash());

    // check that blocks aren't linked
    ApprovedRB db_prev_r, db_cur_r;
    ASSERT_FALSE(store->request_block_get(db_prev_r_hash, db_prev_r));
    ASSERT_FALSE(store->request_block_get(db_cur_r_hash, db_cur_r));

    ASSERT_TRUE(db_prev_r.next.is_zero());
    ASSERT_TRUE(db_cur_r.previous.is_zero());
}

// Test linking through request block (PersistenceManager<R>::StoreRequestBlock), after epoch block proposal
TEST (DualRBTip, RequestBlockLinking2)
{
    logos::block_store *store(get_and_setup_db());

    // Params setup
    uint32_t cur_epoch (10);
    uint8_t delegate (15);

    // Store epoch block twice previous, previous epoch, epoch tip, prev epoch's last request block
    auto block = create_eb_preprepare();
    AggSignature sig;
    ApprovedEB preprev_e(block, sig, sig);
    ApprovedRB prev_r;

    preprev_e.epoch_number = cur_epoch - 2;
    prev_r.epoch_number = cur_epoch - 1;
    prev_r.primary_delegate = delegate;
    {
        logos::transaction txn(store->environment, nullptr, true);

        ASSERT_FALSE(store->epoch_put(preprev_e, txn));
        ASSERT_FALSE(store->epoch_tip_put(preprev_e.CreateTip(), txn));
        ASSERT_FALSE(store->request_block_put(prev_r, txn));
        Tip empty;
        for (uint8_t d = 0; d < NUM_DELEGATES; d++)
        {
            if (d == delegate)
            {
                ASSERT_FALSE(store->request_tip_put(delegate, prev_r.epoch_number, prev_r.CreateTip(), txn));
            }
            else // populate so LinkAndUpdateTips work correctly
            {
                ASSERT_FALSE(store->request_tip_put(d, prev_r.epoch_number, empty, txn));
            }
        }
    }

    // Simulate apply previous epoch block
    ApprovedEB prev_e(block, sig, sig);
    prev_e.epoch_number = cur_epoch - 1;
    prev_e.previous = preprev_e.Hash();
    PersistenceManager<ConsensusType::Epoch> epoch_persistence (*store,
                                                                std::make_shared<ConsensusReservations>(*store));
    epoch_persistence.ApplyUpdates(prev_e);

    // check that prev hash has been removed
    Tip db_prev_r_tip;
    BlockHash & db_prev_r_hash = db_prev_r_tip.digest;
    ASSERT_TRUE(store->request_tip_get(delegate, cur_epoch - 1, db_prev_r_tip));

    // store current epoch's first request block and link
    ApprovedRB cur_r;
    cur_r.epoch_number = cur_epoch;
    cur_r.primary_delegate = delegate;

    PersistenceManager<ConsensusType::Request> request_persistence (*store,
                                                                    std::make_shared<ConsensusReservations>(*store));

    // Request persistence StoreRequestBlock should link
    {
        logos::transaction txn(store->environment, nullptr, true);
        request_persistence.StoreRequestBlock(cur_r, txn, delegate);
    }

    // check that hashes match
    Tip db_cur_r_tip;
    BlockHash & db_cur_r_hash = db_cur_r_tip.digest;
    ASSERT_FALSE(store->request_tip_get(delegate, cur_epoch, db_cur_r_tip));
    ASSERT_EQ(db_cur_r_hash, cur_r.Hash());

    // check that blocks are linked
    ApprovedRB db_prev_r, db_cur_r;
    ASSERT_FALSE(store->request_block_get(db_cur_r_hash, db_cur_r));
    ASSERT_FALSE(store->request_block_get(db_cur_r.previous, db_prev_r));

    ASSERT_EQ(db_prev_r.next, db_cur_r_hash);
}

// Test epoch and request possible race condition
TEST (DualRBTip, RaceLinking)
{
    auto block = create_eb_preprepare();
    AggSignature sig;
    for (int i = 0; i < 5; i++)
    {
        logos::block_store *store(get_and_setup_db());

        // Params setup
        uint32_t cur_epoch (10);
        uint8_t delegate (15);

        // Store epoch block twice previous, previous epoch, epoch tip, prev epoch's last request block
        ApprovedEB preprev_e(block, sig, sig);
        ApprovedRB prev_r;

        preprev_e.epoch_number = cur_epoch - 2;
        prev_r.epoch_number = cur_epoch - 1;
        prev_r.primary_delegate = delegate;
        {
            logos::transaction txn(store->environment, nullptr, true);

            ASSERT_FALSE(store->epoch_put(preprev_e, txn));
            ASSERT_FALSE(store->epoch_tip_put(preprev_e.CreateTip(), txn));
            ASSERT_FALSE(store->request_block_put(prev_r, txn));
            Tip empty;
            for (uint8_t d = 0; d < NUM_DELEGATES; d++)
            {
                if (d == delegate)
                {
                    ASSERT_FALSE(store->request_tip_put(delegate, prev_r.epoch_number, prev_r.CreateTip(), txn));
                }
                else // populate so LinkAndUpdateTips work correctly
                {
                    ASSERT_FALSE(store->request_tip_put(d, prev_r.epoch_number, empty, txn));
                }
            }
        }

        // Simulate apply previous epoch block
        ApprovedEB prev_e(block, sig, sig);
        prev_e.epoch_number = cur_epoch - 1;
        prev_e.previous = preprev_e.Hash();
        PersistenceManager<ConsensusType::Epoch> epoch_persistence (*store,
                                                                    std::make_shared<ConsensusReservations>(*store));

        // store current epoch's first request block and link
        ApprovedRB cur_r;
        cur_r.epoch_number = cur_epoch;
        cur_r.primary_delegate = delegate;

        PersistenceManager<ConsensusType::Request> request_persistence (*store,
                                                                        std::make_shared<ConsensusReservations>(*store));

        // execute both operations in threads
        std::mt19937_64 eng{std::random_device{}()};
        std::thread t1([&] {
            std::uniform_int_distribution<> dist{10, 100};
            std::this_thread::sleep_for(std::chrono::milliseconds{dist(eng)});
            epoch_persistence.ApplyUpdates(prev_e);
        });
        // Request persistence StoreRequestBlock should link
        std::thread t2([&] {
            std::uniform_int_distribution<> dist{10, 100};
            std::this_thread::sleep_for(std::chrono::milliseconds{dist(eng)});
            logos::transaction txn(store->environment, nullptr, true);
            request_persistence.StoreRequestBlock(cur_r, txn, delegate);
        });

        t1.join();
        t2.join();

        // check that prev hash has been removed
        Tip db_prev_r_tip;
        BlockHash &db_prev_r_hash = db_prev_r_tip.digest;
        ASSERT_TRUE(store->request_tip_get(delegate, cur_epoch - 1, db_prev_r_tip));

        // check that hashes match
        Tip db_cur_r_tip;
        BlockHash & db_cur_r_hash = db_cur_r_tip.digest;
        ASSERT_FALSE(store->request_tip_get(delegate, cur_epoch, db_cur_r_tip));
        ASSERT_EQ(db_cur_r_hash, cur_r.Hash());

        // check that blocks are linked
        ApprovedRB db_prev_r, db_cur_r;
        ASSERT_FALSE(store->request_block_get(db_cur_r_hash, db_cur_r));
        ASSERT_FALSE(store->request_block_get(db_cur_r.previous, db_prev_r));

        ASSERT_EQ(db_prev_r.next, db_cur_r_hash);
    }
}
