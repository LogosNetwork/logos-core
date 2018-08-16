//===-- logos/consensus/microblock_consensus_connection.cpp - MicroBlockConsensusConnection class implementation -------*- C++ -*-===//
//
// Open source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains implementation of the MicroBlockConsensusConnection class, which
/// handles specifics of MicroBlock consensus
///
//===----------------------------------------------------------------------===//
#include <logos/consensus/microblock_consensus_connection.hpp>

bool MicroBlockConsensusConnection::Validate(const PrePrepareMessage<ConsensusType::MicroBlock> & message)
{
    return true;
}

void MicroBlockConsensusConnection::ApplyUpdates(const PrePrepareMessage<ConsensusType::MicroBlock> & block, uint8_t delegate_id)
{
    return;
}