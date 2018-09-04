/// @file
/// This file contains specializations of the ConsensusConnection class, which
/// handle the specifics of BatchBlock consensus.
#include <logos/consensus/consensus_connection.hpp>

/// Validate BatchStateBlock message.
///
///     @param message message to validate
///     @return true if validated false otherwise
template<>
bool 
ConsensusConnection<ConsensusType::BatchStateBlock>::Validate(
    const PrePrepare & message)
{
    for(uint64_t i = 0; i < message.block_count; ++i)
    {
        if(!_persistence_manager.Validate(message.blocks[i], _delegate_ids.remote))
        {
            return false;
        }
    }

    return true;
}

/// Commit the block to the database.
///
///     @param block to commit to the database
///     @param remote delegate id
template<>
void 
ConsensusConnection<ConsensusType::BatchStateBlock>::ApplyUpdates(
    const PrePrepare & block,
    uint8_t delegate_id)
{
    _persistence_manager.ApplyUpdates(block, delegate_id);
}