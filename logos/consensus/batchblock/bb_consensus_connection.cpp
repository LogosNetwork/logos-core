/// @file
/// This file contains specializations of the ConsensusConnection class, which
/// handle the specifics of BatchBlock consensus.
#include <logos/consensus/batchblock/bb_consensus_connection.hpp>

BBConsensusConnection::BBConsensusConnection(
        std::shared_ptr<IOChannel> iochannel,
        PrimaryDelegate & primary,
        RequestPromoter<ConsensusType::BatchStateBlock> & promoter,
        PersistenceManager & persistence_manager,
        MessageValidator & validator,
        const DelegateIdentities & ids,
        BlocksCallback & blocks_callback)
    : Connection(iochannel, primary, promoter,
                 validator, ids, blocks_callback)
    , _persistence_manager(persistence_manager)
{}

bool BBConsensusConnection::IsPrePrepared(const logos::block_hash & hash)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if(!_cur_pre_prepare)
    {
        return false;
    }

    for(uint64_t i = 0; i < _cur_pre_prepare->block_count; ++i)
    {
        if(hash == _cur_pre_prepare->blocks[i].hash())
        {
            return true;
        }
    }

    return false;
}

/// Validate BatchStateBlock message.
///
///     @param message message to validate
///     @return true if validated false otherwise
bool
BBConsensusConnection::Validate(
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
void
BBConsensusConnection::ApplyUpdates(
    const PrePrepare & block,
    uint8_t delegate_id)
{
    _persistence_manager.ApplyUpdates(block, delegate_id);
}
