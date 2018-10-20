/// @file
/// This file contains implementation of the EpochHandler class, which is used
/// in the Epoch processing
#include <logos/node/node_identity_manager.hpp>
#include <logos/epoch/epoch_handler.hpp>
#include <logos/blockstore.hpp>

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
        BOOST_LOG(_log) << "EpochHandler::Validate account doesn't exist " <<
                        epoch.account.to_account();
        return false;
    }

    if (_store.epoch_tip_get(previous_epoch_hash))
    {
        BOOST_LOG(_log) << "EpochHandler::Validate failed to get epoch tip";
        return false;
    }

    if (_store.epoch_get(previous_epoch_hash, previous_epoch))
    {
        BOOST_LOG(_log) << "EpochHandler::Validate failed to get epoch: " <<
            previous_epoch_hash.to_string();
        return false;
    }

    // verify epoch number = previous + 1
    if (epoch.epoch_number != (previous_epoch.epoch_number + 1))
    {
        BOOST_LOG(_log) << "EpochHandler::Validate account invalid epoch number " <<
                        epoch.epoch_number << " " << previous_epoch.epoch_number;
        return false;
    }

    // verify microblock tip exists
    BlockHash micro_block_tip;
    if (_store.micro_block_tip_get(micro_block_tip) || epoch.micro_block_tip != micro_block_tip)
    {
        BOOST_LOG(_log) << "EpochHandler::Validate previous micro block doesn't exist " <<
                        epoch.micro_block_tip.to_string() << " " << micro_block_tip.to_string();
        return false;
    }

    if (!_voting_manager.ValidateEpochDelegates(epoch.delegates))
    {
        BOOST_LOG(_log) << "EpochHandler::Validate invalid deligates ";
        return false;
    }

    // verify transaction fee pool? TBD
    BOOST_LOG(_log) << "EpochHandler::Validate  WARNING TRANSACTION POOL IS NOT VALIDATED";

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
        BOOST_LOG(_log) << "EpochHandler::Build failed to get epoch tip";
        return false;
    }

    if (_store.epoch_get(previous_epoch_hash, previous_epoch))
    {
        BOOST_LOG(_log) << "EpochHandler::Build failed to get epoch: " <<
            previous_epoch_hash.to_string();
        return false;
    }

    if (_store.micro_block_tip_get(previous_micro_block_hash))
    {
        BOOST_LOG(_log) << "EpochHandler::Build failed to get micro block tip";
        return false;
    }

    if (_store.micro_block_get(previous_micro_block_hash, last_micro_block))
    {
        BOOST_LOG(_log) << "EpochHandler::Build failed to get micro block: " <<
            previous_micro_block_hash.to_string();
        return false;
    }

    epoch.previous = previous_epoch_hash;
    epoch.account = NodeIdentityManager::_delegate_account;
    epoch.epoch_number = previous_epoch.epoch_number + 1;
    epoch.micro_block_tip = previous_micro_block_hash;
    _voting_manager.GetNextEpochDelegates(epoch.delegates);
    epoch.transaction_fee_pool = 0; // where does it come from? TBD

    return true;
}
