/// @file
/// This file contains declaration of MicroBlock related validation and persistence

#include <logos/consensus/persistence/persistence_manager.hpp>
#include <logos/lib/trace.hpp>

PersistenceManager<MBCT>::PersistenceManager(Store & store,
                                              ReservationsProvider &)
    : _store(store)
{}

PersistenceManager<MBCT>::PersistenceManager(Store & store)
    : _store(store)
{}

void
PersistenceManager<MBCT>::BatchBlocksIterator(
    const BatchTips &start,
    const BatchTips &end,
    IteratorBatchBlockReceiverCb batchblock_receiver)
{
    for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate)
    {
        BlockHash hash = start[delegate];
        BatchStateBlock batch;
        bool not_found = false;
        for (not_found = _store.batch_block_get(hash, batch);
             !not_found && hash != end[delegate];
             hash = batch.previous, not_found = _store.batch_block_get(hash, batch)) {
            batchblock_receiver(delegate, batch);
        }
        if (not_found && hash != 0)
        {
            LOG_ERROR(_log) << "MicroBlockHander::BatchBlocksIterator failed to get batch state block: "
                            << hash.to_string();
            return;
        }
    }
}

bool
PersistenceManager<MBCT>::Validate(
    const PrePrepare & block)
{
    BlockHash hash = block.Hash();

    // block exists
    if (_store.micro_block_exists(hash))
    {
        LOG_WARN(_log) << "PersistenceManager::VerifyMicroBlock micro block exists "
                       << hash.to_string();
        return true;
    }

    // Account exists
    logos::account_info info;
    if (_store.account_get(block.account, info))
    {
        LOG_ERROR(_log) << "PersistenceManager::VerifyMicroBlock account doesn't exist "
                        << " hash " << hash.to_string()
                        << " account " << block.account.to_account();
        return false;
    }

    Epoch previous_epoch;
    MicroBlock previous_microblock;

    // previous microblock doesn't exist
    if (_store.micro_block_get(block.previous, previous_microblock))
    {
        LOG_ERROR(_log) << "PersistenceManager::VerifyMicroBlock previous doesn't exist "
                        << " hash " << hash.to_string()
                        << " previous " << block.previous.to_string();
        return false;
    }

    if (_store.epoch_tip_get(hash))
    {
        LOG_FATAL(_log) << "PersistenceManager::VerifyMicroBlock failed to get epoch tip "
                        << " hash " << hash.to_string();
        trace_and_halt();
    }

    if (_store.epoch_get(hash, previous_epoch))
    {
        LOG_FATAL(_log) << "PersistenceManager::VerifyMicroBlock failed to get epoch: "
                        << hash.to_string();
        trace_and_halt();
    }

    // previous and proposed microblock are in the same epoch
    if (block.epoch_number == previous_microblock.epoch_number)
    {
        if (block.sequence != (previous_microblock.sequence + 1))
        {
            LOG_ERROR(_log) << "PersistenceManager::VerifyMicroBlock failed, invalid sequence # "
                            << " hash " << block.hash().to_string()
                            << " epoch #: " << block.epoch_number << " block seq #:" << block.sequence
                            << " previous block seq #:" << previous_microblock.sequence
                            << " previous hash " << block.previous.to_string();
            return false;
        }
    }
    // proposed microblock must be in new epoch
    else if (block.epoch_number != (previous_microblock.epoch_number + 1) ||
            block.sequence != 0)
    {
        LOG_ERROR(_log) << "PersistenceManager::VerifyMicroBlock failed, bad epoch number or sequence not 0 "
                        << " hash " << block.hash().to_string()
                        << " epoch #: " << block.epoch_number << " previous block epoch #:"
                        << previous_microblock.epoch_number << " block seq #:" << block.sequence
                        << " previous hash " << block.previous.to_string();
        return false;
    }

    /// verify can iterate the chain and the number of blocks checks out
    int number_batch_blocks = 0;
    BatchBlocksIterator(block.tips, previous_microblock.tips, [&number_batch_blocks](uint8_t, const BatchStateBlock &) mutable -> void {
       ++number_batch_blocks;
    });
    if (number_batch_blocks != block.number_batch_blocks)
    {
        LOG_ERROR(_log) << "PersistenceManager::VerifyMicroBlock number of batch blocks doesn't match in block: "
                        << " hash " << block.hash().to_string()
                        << " block " << block.number_batch_blocks << " to database: " << number_batch_blocks;
        return false;
    }

    // verify can get the batch block tips
    BatchStateBlock bsb;
    for (int del = 0; del < NUM_DELEGATES; ++del)
    {
        if (block.tips[del] != 0 && _store.batch_block_get(block.tips[del], bsb))
        {
            LOG_FATAL(_log) << "PersistenceManager::VerifyMicroBlock failed to get batch tip: "
                            << block.hash().to_string() << " "
                            << block.tips[del].to_string();
            trace_and_halt();
        }
    }

    return true;
}

void
PersistenceManager<MBCT>::ApplyUpdates(
    const PrePrepare & block,
    uint8_t)
{
    logos::transaction transaction(_store.environment, nullptr, true);
    BlockHash hash = _store.micro_block_put(block, transaction);
    _store.micro_block_tip_put(hash, transaction);
    MicroBlock previous;
    if (_store.micro_block_get(block.previous, previous, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::ApplyUpdates failed to get previous block "
                        << block.previous.to_string();
        trace_and_halt();
    }
    previous.next = hash;
    hash = _store.micro_block_put(previous, transaction);
    LOG_INFO(_log) << "PersistenceManager::ApplyUpdates hash: " << hash.to_string()
                   << " previous " << hash.to_string();
}