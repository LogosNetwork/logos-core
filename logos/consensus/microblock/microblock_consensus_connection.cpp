/// @file
/// This file contains specializations of the ConsensusConnection class which
/// handle the specifics of MicroBlock consensus.
#include <logos/consensus/consensus_connection.hpp>

/// Validate MicroBlock.
///     @param message message to validate
///     @return true if validated false otherwise
template<>
bool 
ConsensusConnection<ConsensusType::MicroBlock>::Validate(
    const PrePrepare & message)
{
    return true;
}

/// Commit the block to the database.
///
///     @param block block to commit to the database
///     @param delegate_id remote delegate id
template<>
void 
ConsensusConnection<ConsensusType::MicroBlock>::ApplyUpdates(
    const PrePrepare & block,
    uint8_t delegate_id)
{
    return;
}