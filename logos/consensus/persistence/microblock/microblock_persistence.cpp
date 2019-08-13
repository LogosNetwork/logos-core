/// @file
/// This file contains declaration of MicroBlock related validation and persistence

#include <logos/consensus/persistence/microblock/microblock_persistence.hpp>
#include <logos/microblock/microblock_handler.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/lib/trace.hpp>
#include <logos/node/node.hpp>

bool
PersistenceManager<MBCT>::Validate(
    const PrePrepare & block,
    ValidationStatus * status)
{
    using namespace logos;
    LOG_TRACE(_log) << "PersistenceManager<MBCT>::Validate {";

    if (!status || status->progress < MVP_BASE)
    {
        BlockHash hash = block.Hash();
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
        Tip tip;
        if (_store.epoch_tip_get(tip))
        {
            LOG_FATAL(_log) << "PersistenceManager::VerifyMicroBlock failed to get epoch tip "
                            << " hash " << hash.to_string();
            trace_and_halt();
        }
        hash = tip.digest;

        if (_store.epoch_get(hash, previous_epoch))
        {
            LOG_FATAL(_log) << "PersistenceManager::VerifyMicroBlock failed to get epoch: "
                            << hash.to_string();
            trace_and_halt();
        }

        if (status)
            status->progress = MVP_BASE;
    }

    if (!status || status->progress < MVP_TIPS_DONE)
    {
        // verify can get the batch block tips
        bool valid = true;
        ApprovedRB bsb;
        for (int del = 0; del < NUM_DELEGATES; ++del)
        {
            if (!status || status->progress < MVP_TIPS_FIRST || status->requests.find(del) != status->requests.end())
            {
                if (! block.tips[del].digest.is_zero()
                    && _store.request_block_get(block.tips[del].digest, bsb))
                {
                    LOG_ERROR   (_log) << "PersistenceManager::VerifyMicroBlock failed to get batch tip: "
                                    << block.Hash().to_string() << " "
                                    << block.tips[del].to_string();
                    UpdateStatusReason(status, process_result::invalid_request);
                    UpdateStatusRequests(status, del, process_result::gap_previous);
                    valid = false;
                }
                else if (status && status->progress >= MVP_TIPS_FIRST)
                {
                    status->requests.erase(del);
                }
            }
        }

        if (status)
            status->progress = MVP_TIPS_FIRST;

        if (!valid)
            return false;

        if (status)
            status->progress = MVP_TIPS_DONE;
    }

    if (!status || status->progress < MVP_END)
    {
        ApprovedMB previous_microblock;

        // previous microblock doesn't exist
        if (_store.micro_block_get(block.previous, previous_microblock))
        {
            LOG_ERROR(_log) << "PersistenceManager::VerifyMicroBlock previous doesn't exist "
                            << " hash " << block.Hash().to_string()
                            << " previous " << block.previous.to_string();
            UpdateStatusReason(status, process_result::gap_previous);

            // TODO: high speed bootstrap
            logos_global::Bootstrap();

            return false;
        }

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

        /// verify the number of blocks
        int number_batch_blocks = 0;
        for (int del = 0; del < NUM_DELEGATES; ++del)
        {
            auto new_n = block.tips[del].n_th_block_in_epoch(block.epoch_number);
            auto old_n = previous_microblock.tips[del].n_th_block_in_epoch(block.epoch_number);
            number_batch_blocks += new_n - old_n;
        }
        if (number_batch_blocks != block.number_batch_blocks)
        {
            LOG_ERROR(_log) << "PersistenceManager::VerifyMicroBlock number of batch blocks doesn't match in block: "
                            << " hash " << block.Hash().to_string()
                            << " block " << block.number_batch_blocks << " to database: " << number_batch_blocks;
            UpdateStatusReason(status, process_result::invalid_number_blocks);
            return false;
        }

        if (status)
            status->progress = MVP_END;
    }

    LOG_TRACE(_log) << "PersistenceManager<MBCT>::Validate Good}";
    return true;
}

void
PersistenceManager<MBCT>::ApplyUpdates(
    const ApprovedMB & block,
    uint8_t)
{
    logos::transaction transaction(_store.environment, nullptr, true);

    // See comments in request_persistence.cpp
    if (BlockExists(block))
    {
        LOG_DEBUG(_log) << "PersistenceManager<MBCT>::ApplyUpdates - micro block already exists, ignoring";
        return;
    }

    BlockHash hash = block.Hash();
    if( _store.micro_block_put(block, transaction) ||
            _store.micro_block_tip_put(block.CreateTip(), transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager<MBCT>::ApplyUpdates failed to put block or tip"
                                << hash.to_string();
        trace_and_halt();
    }

    if(_store.consensus_block_update_next(block.previous, hash, ConsensusType::MicroBlock, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager<MBCT>::ApplyUpdates failed to get previous block "
                        << block.previous.to_string();
        trace_and_halt();
    }
    LOG_INFO(_log) << "PersistenceManager<MBCT>::ApplyUpdates hash: " << hash.to_string()
                   << " previous " << block.previous.to_string();
}

bool PersistenceManager<MBCT>::BlockExists(
    const ApprovedMB & message)
{
    return _store.micro_block_exists(message);
}
