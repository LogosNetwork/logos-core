/// @file
/// This file contains declaration of MicroBlock related validation and persistence

#include <logos/consensus/persistence/microblock/microblock_persistence.hpp>
#include <logos/microblock/microblock_handler.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/lib/trace.hpp>

#include <mutex>

static std::mutex global_mutex; // RGD

bool
PersistenceManager<MBCT>::Validate(
    const PrePrepare & block,
    ValidationStatus * status)
{
    std::lock_guard<std::mutex> lock(global_mutex);

    BlockHash hash = block.Hash();
    using namespace logos;

    // block exists
    if (_store.micro_block_exists(hash))
    {
        LOG_WARN(_log) << "PersistenceManager::VerifyMicroBlock micro block exists "
                       << hash.to_string();
        return true;
    }

    if (block.primary_delegate >= NUM_DELEGATES)
    {
        UpdateStatusReason(status, process_result::invalid_request);
        LOG_ERROR(_log) << "PersistenceManager::Validate primary index out of range " << (int) block.primary_delegate;
        return false;
    }

    ApprovedEB previous_epoch;
    ApprovedMB previous_microblock;

    // previous microblock doesn't exist
    if (_store.micro_block_get(block.previous, previous_microblock))
    {
        LOG_ERROR(_log) << "PersistenceManager::VerifyMicroBlock previous doesn't exist "
                        << " hash " << hash.to_string()
                        << " previous " << block.previous.to_string();
        UpdateStatusReason(status, process_result::gap_previous);
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
                            << " hash " << block.Hash().to_string()
                            << " epoch #: " << block.epoch_number << " block seq #:" << block.sequence
                            << " previous block seq #:" << previous_microblock.sequence
                            << " previous hash " << block.previous.to_string();
            UpdateStatusReason(status, process_result::wrong_sequence_number);
            return false;
        }
    }
    // proposed microblock must be in new epoch
    else if (block.epoch_number != (previous_microblock.epoch_number + 1) ||
            block.sequence != 0)
    {
        LOG_ERROR(_log) << "PersistenceManager::VerifyMicroBlock failed, bad epoch number or sequence not 0 "
                        << " hash " << block.Hash().to_string()
                        << " epoch #: " << block.epoch_number << " previous block epoch #:"
                        << previous_microblock.epoch_number << " block seq #:" << block.sequence
                        << " previous hash " << block.previous.to_string();
        UpdateStatusReason(status, process_result::wrong_sequence_number);
        return false;
    }

    /// verify can iterate the chain and the number of blocks checks out
    int number_batch_blocks = 0;
    MicroBlockHandler::BatchBlocksIterator(_store, block.tips, previous_microblock.tips,
                                           [&number_batch_blocks](uint8_t, const BatchStateBlock &) mutable -> void {
       ++number_batch_blocks;
    });
    if (number_batch_blocks != block.number_batch_blocks)
    {
        LOG_ERROR(_log) << "PersistenceManager::VerifyMicroBlock number of batch blocks doesn't match in block: "
                        << " hash " << block.Hash().to_string()
                        << " block " << block.number_batch_blocks << " to database: " << number_batch_blocks;
        UpdateStatusReason(status, process_result::invalid_number_blocks);
        return false;
    }

    // verify can get the batch block tips
    bool valid = true;
    ApprovedBSB bsb;
    for (int del = 0; del < NUM_DELEGATES; ++del)
    {
        if (! block.tips[del].is_zero() && _store.batch_block_get(block.tips[del], bsb))
        {
            LOG_ERROR   (_log) << "PersistenceManager::VerifyMicroBlock failed to get batch tip: "
                            << block.Hash().to_string() << " "
                            << block.tips[del].to_string();
            UpdateStatusReason(status, process_result::gap_previous);
            UpdateStatusRequests(status, del, process_result::gap_previous);
            valid = false;
        }
    }

    return valid;
}

void
PersistenceManager<MBCT>::ApplyUpdates(
    const ApprovedMB & block,
    uint8_t)
{
    std::lock_guard<std::mutex> lock(global_mutex);

    logos::transaction transaction(_store.environment, nullptr, true);
    BlockHash hash = block.Hash();
    if( _store.micro_block_put(block, transaction) ||
            _store.micro_block_tip_put(hash, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::ApplyUpdates failed to put block or tip"
                                << hash.to_string();
        trace_and_halt();
    }

    if(_store.consensus_block_update_next(block.previous, hash, ConsensusType::MicroBlock, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::ApplyUpdates failed to get previous block "
                        << block.previous.to_string();
        trace_and_halt();
    }
    LOG_INFO(_log) << "PersistenceManager::ApplyUpdates hash: " << hash.to_string()
                   << " previous " << block.previous.to_string();
}
