#include <logos/consensus/batchblock_consensus_connection.hpp>

bool BatchBlockConsensusConnection::Validate(const PrePrepareMessage<ConsensusType::BatchStateBlock> & message)
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
