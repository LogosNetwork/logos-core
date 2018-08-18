//===-- logos/consensus/batchblock_consensus_connection.cpp - BatchBlockConsensusConnection class implementation -------*- C++ -*-===//
//
// Open source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains implementation of the BatchBlockConsensusConnection class, which
/// handles specifics of BatchBlock consensus
///
//===----------------------------------------------------------------------===//
#include <logos/consensus/batchstateblock/batchblock_consensus_connection.hpp>

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

void BatchBlockConsensusConnection::ApplyUpdates(const PrePrepareMessage<ConsensusType::BatchStateBlock> & block, uint8_t delegate_id)
{
    _persistence_manager.ApplyUpdates(block, delegate_id);
}