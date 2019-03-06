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
        LOG_ERROR(_log) << "PersistenceManager::Validate invalid deligates ";
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
    // starting from epoch 4 (i.e. one after Genesis)
    if (block.epoch_number <= GENESIS_EPOCH + 1)
    {
        return;
    }

    using BatchTips = BlockHash[NUM_DELEGATES];
    BatchTips start, end, cur_e_first, prev_e_last;
    for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate)
    {
        if (_store.batch_tip_get(delegate, block.epoch_number, start[delegate]))
        {
            LOG_FATAL(_log) << "PersistenceManager<ECT>::ApplyUpdates failed to get request block tip for delegate "
                            << delegate << " for epoch number " << block.epoch_number;
            trace_and_halt();
        }
    }
    MicroBlockHandler::BatchBlocksIterator(_store, start, end, [&](uint8_t delegate, const ApprovedBSB &batch)mutable->void{
        if (batch.previous.is_zero())
        {
            cur_e_first[delegate] = batch.Hash();
        }
    });

    // Update `previous` of first request block in epoch + Update `next` of last request block in previous epoch
    for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate)
    {
        if (_store.batch_tip_get(delegate, block.epoch_number - 1, prev_e_last[delegate]))
        {
            LOG_FATAL(_log) << "PersistenceManager<ECT>::ApplyUpdates failed to get request block tip for delegate "
                            << delegate << " for epoch number " << block.epoch_number - 1;
            trace_and_halt();
        }

        if (_store.consensus_block_update_next(prev_e_last[delegate], cur_e_first[delegate], ConsensusType::BatchStateBlock, transaction))
        {
            LOG_FATAL(_log) << "PersistenceManager<ECT>::ApplyUpdates failed to update prev epoch's "
                            << "request block tip for delegate " << delegate;
            trace_and_halt();
        }

        if (_store.request_block_update_prev(cur_e_first[delegate], prev_e_last[delegate], transaction))
        {
            LOG_FATAL(_log) << "PersistenceManager<ECT>::ApplyUpdates failed to update current epoch's "
                            << "first request block prev for delegate " << delegate;
            trace_and_halt();
        }

        _store.batch_tip_del(delegate, block.epoch_number - 1, transaction);
    }
}

bool PersistenceManager<ECT>::BlockExists(
    const ApprovedEB & message)
{
    return _store.epoch_exists(message);
}
