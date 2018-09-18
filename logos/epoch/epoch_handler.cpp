/// @file
/// This file contains implementation of the EpochHandler class, which is used
/// in the Epoch processing
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
    if (_store.account_get(epoch._account, info))
    {
        BOOST_LOG(_log) << "MicroBlockHandler::VerifyMicroBlock account doesn't exist " <<
                        epoch._account.to_account();
        return false;
    }

    assert(false == _store.epoch_tip_get(previous_epoch_hash));
    assert(false == _store.epoch_get(previous_epoch_hash, previous_epoch));

    // verify epoch number = previous + 1
    if (epoch._epoch_number != (previous_epoch._epoch_number + 1))
    {
        BOOST_LOG(_log) << "MicroBlockHandler::VerifyMicroBlock account invalid epoch number " <<
                        epoch._epoch_number << " " << previous_epoch._epoch_number;
        return false;
    }

    // verify microblock tip exists
    BlockHash micro_block_tip;
    if (_store.micro_block_tip_get(micro_block_tip) || epoch._micro_block_tip != micro_block_tip)
    {
        BOOST_LOG(_log) << "MicroBlockHandler::VerifyMicroBlock previous micro block doesn't exist " <<
                        epoch._micro_block_tip.to_string() << " " << micro_block_tip.to_string();
        return false;
    }

    if (!_voting_manager.ValidateEpochDelegates(epoch._delegates))
    {
        BOOST_LOG(_log) << "MicroBlockHandler::VerifyMicroBlock invalid deligates ";
        return false;
    }

    // verify transaction fee pool? TBD
    BOOST_LOG(_log) << "MicroBlockHandler::VerifyMicroBlock  WARNING TRANSACTION POOL IS NOT VALIDATED";

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

void
EpochHandler::BuildEpochBlock(Epoch &epoch)
{
    BlockHash previous_epoch_hash;
    BlockHash previous_micro_block_hash;
    Epoch previous_epoch;
    MicroBlock last_micro_block;

    assert(false == _store.epoch_tip_get(previous_epoch_hash));
    assert(false == _store.epoch_get(previous_epoch_hash, previous_epoch));
    assert(false == _store.micro_block_tip_get(previous_micro_block_hash));
    assert(false == _store.micro_block_get(previous_micro_block_hash, last_micro_block));

    epoch.previous = previous_epoch_hash;
    epoch._account = logos::genesis_delegates[_delegate_id].key.pub;
    epoch._epoch_number = previous_epoch._epoch_number + 1;
    epoch._micro_block_tip = previous_micro_block_hash;
    _voting_manager.GetNextEpochDelegates(epoch._delegates);
    epoch._transaction_fee_pool = 0; // where does it come from?
    epoch._signature = logos::sign_message(logos::genesis_delegates[_delegate_id].key.prv,
                                           logos::genesis_delegates[_delegate_id].key.pub, epoch.hash());
}
