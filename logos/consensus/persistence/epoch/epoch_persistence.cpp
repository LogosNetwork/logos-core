/// @file
/// This file contains declaration of Epoch related validation and persistence

#include <logos/consensus/persistence/epoch/epoch_persistence.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
#include <logos/microblock/microblock_handler.hpp>
#include <logos/lib/trace.hpp>

PersistenceManager<ECT>::PersistenceManager(Store & store,
                                            ReservationsPtr,
                                            Milliseconds clock_drift)
    : Persistence(store, clock_drift)
{}

bool
PersistenceManager<ECT>::Validate(
    const PrePrepare & epoch,
    ValidationStatus * status)
{
    BlockHash previous_epoch_hash;
    ApprovedEB previous_epoch;
    using namespace logos;

    if (epoch.primary_delegate >= NUM_DELEGATES)
    {
        UpdateStatusReason(status, process_result::invalid_request);
        LOG_ERROR(_log) << "PersistenceManager::Validate primary index out of range " << (int) epoch.primary_delegate;
        return false;
    }

    if (_store.epoch_tip_get(previous_epoch_hash))
    {
        LOG_FATAL(_log) << "PersistenceManager::Validate failed to get epoch tip";
        trace_and_halt();
    }

    if (_store.epoch_get(previous_epoch_hash, previous_epoch))
    {
        LOG_ERROR(_log) << "PersistenceManager::Validate failed to get epoch: " <<
                        previous_epoch_hash.to_string();
        UpdateStatusReason(status, process_result::gap_previous);
        return false;
    }

    // verify epoch number = previous + 1
    if (epoch.epoch_number != (previous_epoch.epoch_number + 1))
    {
        LOG_ERROR(_log) << "PersistenceManager::Validate account invalid epoch number " <<
                        epoch.epoch_number << " " << previous_epoch.epoch_number;
        UpdateStatusReason(status, process_result::block_position);
        return false;
    }

    // verify microblock tip exists
    BlockHash micro_block_tip;
    if (_store.micro_block_tip_get(micro_block_tip))
    {
        LOG_FATAL(_log) << "PersistenceManager::Validate failed to get microblock tip";
        trace_and_halt();
    }

    if (_store.micro_block_tip_get(micro_block_tip))
    {
        LOG_FATAL(_log) << "PersistenceManager::Validate micro block tip doesn't exist";
        trace_and_halt();
        return false;
    }

    if (epoch.micro_block_tip != micro_block_tip)
    {
        LOG_ERROR(_log) << "PersistenceManager::Validate previous micro block doesn't exist " <<
                        epoch.micro_block_tip.to_string() << " " << micro_block_tip.to_string();
        UpdateStatusReason(status, process_result::invalid_tip);
        return false;
    }

    if (!EpochVotingManager::ValidateEpochDelegates(epoch.delegates))
    {
        LOG_ERROR(_log) << "PersistenceManager::Validate invalid delegates ";
        UpdateStatusReason(status, process_result::not_delegate);
        return false;
    }

    // verify transaction fee pool? TBD
    LOG_WARN(_log) << "PersistenceManager::Validate  WARNING TRANSACTION POOL IS NOT VALIDATED";

    return true;
}

void
PersistenceManager<ECT>::ApplyUpdates(
    const ApprovedEB & block,
    uint8_t)
{
    logos::transaction transaction(_store.environment, nullptr, true);
    BlockHash epoch_hash = block.Hash();

    if(_store.epoch_put(block, transaction) || _store.epoch_tip_put(epoch_hash, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager<ECT>::ApplyUpdates failed to store epoch or epoch tip "
                                << epoch_hash.to_string();
        trace_and_halt();
    }

    if(_store.consensus_block_update_next(block.previous, epoch_hash, ConsensusType::Epoch, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager<ECT>::ApplyUpdates failed to get previous block "
                        << block.previous.to_string();
        trace_and_halt();
    }

    // Link epoch's first request block with previous epoch's last request block
    // starting from epoch 3 (i.e. after Genesis)
    if (block.epoch_number <= GENESIS_EPOCH)
    {
        return;
    }

    using BatchTips = BlockHash[NUM_DELEGATES];
    BatchTips start, end, cur_e_first;

    // `start` is current epoch tip, `end` is empty
    for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate)
    {
        if (_store.request_tip_get(delegate, block.epoch_number + 1, start[delegate]))
        {
            LOG_DEBUG(_log) << "PersistenceManager<ECT>::ApplyUpdates request block tip for delegate "
                            << std::to_string(delegate) << " for epoch number " << block.epoch_number + 1
                            << " doesn't exist yet, setting to zero.";
        }
    }

    // iterate backwards from current tip till the gap (i.e. beginning of this current epoch)
    MicroBlockHandler::BatchBlocksIterator(_store, start, end, [&](uint8_t delegate, const ApprovedRB &batch)mutable->void{
        if (batch.previous.is_zero())
        {
            cur_e_first[delegate] = batch.Hash();
        }
    });

    for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate)
    {
        // Get previous epoch's request block tip
        BlockHash prev_e_last;
        if (_store.request_tip_get(delegate, block.epoch_number, prev_e_last))
        {
            LOG_FATAL(_log) << "PersistenceManager<ECT>::ApplyUpdates failed to get request block tip for delegate "
                            << std::to_string(delegate) << " for epoch number " << block.epoch_number;
            trace_and_halt();
        }

        // Don't connect chains if current epoch doesn't contain a tip yet. See request block persistence for this case
        if (cur_e_first[delegate].is_zero())
        {
            // Use old request block tip for current epoch
            if (_store.request_tip_put(delegate, block.epoch_number + 1, prev_e_last, transaction))
            {
                LOG_FATAL(_log) << "PersistenceManager<ECT>::ApplyUpdates failed to put request block tip for delegate "
                                << std::to_string(delegate) << " for epoch number " << block.epoch_number + 1;
                trace_and_halt();
            }
        }
        else
        {
            // Update `next` of last request block in previous epoch
            if (_store.consensus_block_update_next(prev_e_last, cur_e_first[delegate], ConsensusType::Request, transaction))
            {
                LOG_FATAL(_log) << "PersistenceManager<ECT>::ApplyUpdates failed to update prev epoch's "
                                << "request block tip for delegate " << std::to_string(delegate);
                trace_and_halt();
            }

            // Update `previous` of first request block in epoch
            if (_store.request_block_update_prev(cur_e_first[delegate], prev_e_last, transaction))
            {
                LOG_FATAL(_log) << "PersistenceManager<ECT>::ApplyUpdates failed to update current epoch's "
                                << "first request block prev for delegate " << std::to_string(delegate);
                trace_and_halt();
            }
        }

        _store.request_tip_del(delegate, block.epoch_number, transaction);
    }
}

bool PersistenceManager<ECT>::BlockExists(
    const ApprovedEB & message)
{
    return _store.epoch_exists(message);
}
