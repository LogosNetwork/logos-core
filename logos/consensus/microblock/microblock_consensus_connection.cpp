//===-- logos/consensus/microblock_consensus_connection.cpp - ConsensusConnection class specialization -------*- C++ -*-===//
//
// Open source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains specialization of the ConsensusConnection class, which
/// handles specifics of MicroBlock consensus
///
//===----------------------------------------------------------------------===//
#include <logos/consensus/consensus_connection.hpp>

//!
/*!
  Validate BatchStateBlock message
  \param message message to validate
  \return true if validated false otherwise
*/
template<>
bool 
ConsensusConnection<ConsensusType::MicroBlock>::Validate(
    const PrePrepareMessage<ConsensusType::MicroBlock> & message)
{
    return true;
}


//! Commit the block to the database
/*
  \param block to commit to the database
  \param remote delegate id
*/
template<>
void 
ConsensusConnection<ConsensusType::MicroBlock>::ApplyUpdates(
    const PrePrepareMessage<ConsensusType::MicroBlock> & block, 
    uint8_t delegate_id)
{
    return;
}