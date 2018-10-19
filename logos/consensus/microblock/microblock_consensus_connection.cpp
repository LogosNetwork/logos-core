///
/// @file
/// This file contains definition of the MicroBlockConsensusConnection class
/// which handles specifics of MicroBlock consensus
///
#include <logos/consensus/microblock/microblock_consensus_connection.hpp>
#include <logos/epoch/archiver.hpp>

bool
MicroBlockConsensusConnection::Validate(
    const PrePrepare & message)
{
    return _microblock_handler.Validate(message);
}

void
MicroBlockConsensusConnection::ApplyUpdates(
    const PrePrepare & block,
    uint8_t delegate_id)
{
    _microblock_handler.CommitToDatabase(block);
}

bool
MicroBlockConsensusConnection::IsPrePrepared(
    const logos::block_hash & hash)
{
    std::lock_guard<std::mutex> lock(_mutex);

    return (_cur_pre_prepare && hash == _cur_pre_prepare->hash());
}