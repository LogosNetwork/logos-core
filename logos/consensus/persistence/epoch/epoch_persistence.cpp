/// @file
/// This file contains declaration of Epoch related validation and persistence

#include <logos/consensus/persistence/epoch/epoch_persistence.hpp>
#include <logos/lib/trace.hpp>

PersistenceManager<ECT>::PersistenceManager(Store & store,
                                              ReservationsProvider &)
    : _store(store)
{}

bool
PersistenceManager<ECT>::Validate(
    const PrePrepare & epoch)
{
    BlockHash previous_epoch_hash;
    Epoch previous_epoch;

    // Account must exist
    logos::account_info info;
    if (_store.account_get(epoch.account, info))
    {
        LOG_ERROR(_log) << "PersistenceManager::Validate account doesn't exist " <<
                        epoch.account.to_account();
        return false;
    }

    if (_store.epoch_tip_get(previous_epoch_hash))
    {
        LOG_FATAL(_log) << "PersistenceManager::Validate failed to get epoch tip";
        trace_and_halt();
    }

    if (_store.epoch_get(previous_epoch_hash, previous_epoch))
    {
        LOG_FATAL(_log) << "PersistenceManager::Validate failed to get epoch: " <<
            previous_epoch_hash.to_string();
        trace_and_halt();
    }

    // verify epoch number = previous + 1
    if (epoch.epoch_number != (previous_epoch.epoch_number + 1))
    {
        LOG_ERROR(_log) << "PersistenceManager::Validate account invalid epoch number " <<
                        epoch.epoch_number << " " << previous_epoch.epoch_number;
        return false;
    }

    // verify microblock tip exists
    BlockHash micro_block_tip;
    if (_store.micro_block_tip_get(micro_block_tip))
    {
        LOG_FATAL(_log) << "PersistenceManager::Validate failed to get microblock tip";
        trace_and_halt();
    }

    if (_store.micro_block_tip_get(micro_block_tip) || epoch.micro_block_tip != micro_block_tip)
    {
        LOG_ERROR(_log) << "PersistenceManager::Validate previous micro block doesn't exist " <<
                        epoch.micro_block_tip.to_string() << " " << micro_block_tip.to_string();
        return false;
    }

    /*if (!_voting_manager.ValidateEpochDelegates(epoch.delegates))
    {
        LOG_ERROR(_log) << "PersistenceManager::Validate invalid deligates ";
        return false;
    }*/

    // verify transaction fee pool? TBD
    LOG_WARN(_log) << "PersistenceManager::Validate  WARNING TRANSACTION POOL IS NOT VALIDATED";

    return true;
}

void
PersistenceManager<ECT>::ApplyUpdates(
    const PrePrepare & block,
    uint8_t)
{
    logos::transaction transaction(_store.environment, nullptr, true);
    logos::block_hash  epoch_hash = _store.epoch_put(block, transaction);
    _store.epoch_tip_put(epoch_hash, transaction);
    Epoch previous;
    if (_store.epoch_get(block.previous, previous, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::ApplyUpdate failed to get previous block "
                        << block.previous.to_string();
        trace_and_halt();
    }
    previous.next = epoch_hash;
    _store.epoch_put(previous, transaction);
}