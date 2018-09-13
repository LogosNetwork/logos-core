///
/// @file
/// This file contains definition of the MicroBlockConsensusConnection class
/// which handles specifics of MicroBlock consensus
///
#include <logos/consensus/microblock/microblock_consensus_connection.hpp>

bool
MicroBlockConsensusConnection::Validate(
    const PrePrepare & message)
{
    return _microblock_handler.VerifyMicroBlock(message);
}

void
MicroBlockConsensusConnection::ApplyUpdates(
    const PrePrepare & block,
    uint8_t delegate_id)
{
    _microblock_handler.ApplyUpdates(block);
}