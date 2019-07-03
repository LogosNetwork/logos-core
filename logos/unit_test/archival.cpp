#include <gtest/gtest.h>
#include <logos/consensus/message_handler.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/epoch/recall_handler.hpp>
#include <logos/node/node.hpp>
#include <logos/unit_test/msg_validator_setup.hpp>

TEST (Archival, ShouldSkipMBProposal)
{
    logos::block_store* store (get_db());
    clear_dbs();

    // construct stored mb & store
    uint32_t old_epoch = 10;
    uint32_t old_seq = 15;
    ApprovedMB stored_mb;
    stored_mb.epoch_number = old_epoch;
    stored_mb.sequence = old_seq;

    auto store_mb = [&](ApprovedMB &mb) ->bool{
        logos::transaction txn(store->environment, nullptr, true);
        return store->micro_block_put(mb, txn) || store->micro_block_tip_put(mb.CreateTip(), txn);
    };

    bool error (store_mb(stored_mb));
    ASSERT_FALSE(error);

    // construct Archiver and MicroBlockMessageHandler
    boost::asio::io_service service;
    logos::alarm alarm (service);
    RecallHandler recall_handler;
    Archiver archiver(alarm, *store, recall_handler);

    // 1. Simulate local clock lag (Archiver counter lags behind)
    stored_mb.sequence += 1;
    error = store_mb(stored_mb);
    ASSERT_FALSE(error);

    ASSERT_TRUE(archiver.ShouldSkipMBBuild());
    ASSERT_EQ(archiver._counter, std::make_pair(old_epoch, old_seq + 1));  // side effect: Archiver _counter catches up

    // 2. Simulate unfinished consensus (stored lags behind queued)
    archiver._counter.second += 1;  // match Archiver _counter with queued content
    // construct queued mb & queue it
    auto queued_mb = std::make_shared<DelegateMessage<ConsensusType::MicroBlock>>();
    queued_mb->epoch_number = old_epoch;
    queued_mb->sequence = old_seq + 2;
    MicroBlockMessageHandler::GetMessageHandler().OnMessage(queued_mb);
    ASSERT_TRUE(archiver.ShouldSkipMBBuild());

    // 3. Simulate both above (queued is one ahead of both stored and counter)
    archiver._counter.second -= 1;  // match Archiver _counter with stored content
    ASSERT_TRUE(archiver.ShouldSkipMBBuild());
    ASSERT_EQ(archiver._counter, std::make_pair(old_epoch, old_seq + 2));  // side effect: Archiver _counter catches up

    // 4. Simulate normal scenario (should go ahead and propose)
    stored_mb.sequence += 1;
    error = store_mb(stored_mb);
    ASSERT_FALSE(error);
    ASSERT_FALSE(archiver.ShouldSkipMBBuild());
    ASSERT_EQ(archiver._counter, std::make_pair(old_epoch, old_seq + 2));  // no side effect
}
