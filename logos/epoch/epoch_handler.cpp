/// @file
/// This file contains implementation of the EpochHandler class, which is used
/// in the Epoch processing
#include <logos/node/delegate_identity_manager.hpp>
#include <logos/epoch/epoch_handler.hpp>
#include <logos/blockstore.hpp>
#include <logos/lib/trace.hpp>
#include <logos/lib/log.hpp>

bool
EpochHandler::Validate(
    const Epoch &epoch)
{
    BlockHash previous_epoch_hash;
    Epoch previous_epoch;

    // Account must exist
    logos::account_info info;
    if (_store.account_get(epoch.account, info))
    {
        LOG_ERROR(_log) << "EpochHandler::Validate account doesn't exist " <<
                        epoch.account.to_account();
        return false;
    }

    if (_store.epoch_tip_get(previous_epoch_hash))
    {
        LOG_FATAL(_log) << "EpochHandler::Validate failed to get epoch tip";
        trace_and_halt();
    }

    if (_store.epoch_get(previous_epoch_hash, previous_epoch))
    {
        LOG_FATAL(_log) << "EpochHandler::Validate failed to get epoch: " <<
            previous_epoch_hash.to_string();
        trace_and_halt();
    }

    // verify epoch number = previous + 1
    if (epoch.epoch_number != (previous_epoch.epoch_number + 1))
    {
        LOG_ERROR(_log) << "EpochHandler::Validate account invalid epoch number " <<
                        epoch.epoch_number << " " << previous_epoch.epoch_number;
        return false;
    }

    // verify microblock tip exists
    BlockHash micro_block_tip;
    if (_store.micro_block_tip_get(micro_block_tip))
    {
        LOG_FATAL(_log) << "EpochHandler::Validate failed to get microblock tip";
        trace_and_halt();
    }

    if (_store.micro_block_tip_get(micro_block_tip) || epoch.micro_block_tip != micro_block_tip)
    {
        LOG_ERROR(_log) << "EpochHandler::Validate previous micro block doesn't exist " <<
                        epoch.micro_block_tip.to_string() << " " << micro_block_tip.to_string();
        return false;
    }

    if (!_voting_manager.ValidateEpochDelegates(epoch.delegates))
    {
        LOG_ERROR(_log) << "EpochHandler::Validate invalid deligates ";
        return false;
    }

    // verify transaction fee pool? TBD
    LOG_WARN(_log) << "EpochHandler::Validate  WARNING TRANSACTION POOL IS NOT VALIDATED";

    return true;
}

void
EpochHandler::ApplyUpdates(const Epoch &epoch)
{
    logos::transaction transaction (_store.environment, nullptr, true);
    ApplyUpdates(epoch, transaction);
}

logos::block_hash
EpochHandler::ApplyUpdates(
    const Epoch &epoch,
    const logos::transaction &transaction)
{
    logos::block_hash  epoch_hash = _store.epoch_put(epoch, transaction);
    _store.epoch_tip_put(epoch_hash, transaction);
    Epoch previous;
    if (_store.epoch_get(epoch.previous, previous, transaction))
    {
        LOG_FATAL(_log) << "EpochHandler::ApplyUpdate failed to get previous block "
                        << epoch.previous.to_string();
        trace_and_halt();
    }
    previous.next = epoch_hash;
    _store.epoch_put(previous, transaction);
    return epoch_hash;
}

bool
EpochHandler::Build(Epoch &epoch)
{
    BlockHash previous_epoch_hash;
    BlockHash previous_micro_block_hash;
    Epoch previous_epoch;
    MicroBlock last_micro_block;

    if (_store.epoch_tip_get(previous_epoch_hash))
    {
        LOG_FATAL(_log) << "EpochHandler::Build failed to get epoch tip";
        trace_and_halt();
    }

    if (_store.epoch_get(previous_epoch_hash, previous_epoch))
    {
        LOG_FATAL(_log) << "EpochHandler::Build failed to get epoch: " <<
            previous_epoch_hash.to_string();
        trace_and_halt();
    }

    if (_store.micro_block_tip_get(previous_micro_block_hash))
    {
        LOG_FATAL(_log) << "EpochHandler::Build failed to get micro block tip";
        trace_and_halt();
    }

    if (_store.micro_block_get(previous_micro_block_hash, last_micro_block))
    {
        LOG_FATAL(_log) << "EpochHandler::Build failed to get micro block: " <<
            previous_micro_block_hash.to_string();
        trace_and_halt();
    }

    epoch.timestamp = GetStamp();
    epoch.previous = previous_epoch_hash;
    epoch.account = DelegateIdentityManager::_delegate_account;
    epoch.epoch_number = previous_epoch.epoch_number + 1;
    epoch.micro_block_tip = previous_micro_block_hash;
    _voting_manager.GetNextEpochDelegates(epoch.delegates);
    epoch.transaction_fee_pool = 0; // TODO

    LOG_INFO(_log) << "EpochHandler::Build, built epoch block:"
                   << " hash " << epoch.Hash().to_string()
                   << " timestamp " << epoch.timestamp
                   << " previous " << epoch.previous.to_string()
                   << " epoch_number " << epoch.epoch_number
                   << " micro_block_tip " << epoch.micro_block_tip.to_string();

    return true;
}
