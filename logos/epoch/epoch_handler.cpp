/// @file
/// This file contains implementation of the EpochHandler class, which is used
/// in the Epoch processing
#include <logos/epoch/epoch_handler.hpp>
#include <logos/blockstore.hpp>

bool
EpochHandler::Validate(
    const Epoch &)
{
    return true;
}

void
EpochHandler::ApplyUpdates(const Epoch &epoch)
{
    logos::transaction transaction (_store.environment, nullptr, true);
    ApplyUpdates(epoch);
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
BuildEpochBlock(Epoch &epoch)
{
}
