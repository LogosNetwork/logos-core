///
/// @file
/// This file contains declaration of the EpochConsensusConnection class
/// which handles specifics of Epoch consensus
///
#include <logos/consensus/epoch/epoch_consensus_connection.hpp>
#include <logos/epoch/archiver.hpp>

bool
EpochConsensusConnection::Validate(
    const PrePrepare & message)
{
    return _epoch_handler.Validate(message);
}

void
EpochConsensusConnection::ApplyUpdates(
    const PrePrepare & block,
    uint8_t delegate_id)
{
    _epoch_handler.CommitToDatabase(block);
}

bool
EpochConsensusConnection::IsPrePrepared(
    const logos::block_hash & hash)
{
    std::lock_guard<std::mutex> lock(_mutex);

    return (_cur_pre_prepare && hash == _cur_pre_prepare->hash());
}
