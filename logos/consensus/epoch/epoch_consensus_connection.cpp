//===-- logos/consensus/epoch/epoch_consensus_connection.cpp - ConsensusConnection class specialization -------*- C++ -*-===//
//
// Open source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains specialization of the ConsensusConnection class, which
/// handles specifics of Epoch consensus
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
ConsensusConnection<ConsensusType::Epoch>::Validate(
    const PrePrepareMessage<ConsensusType::Epoch> & message)
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
ConsensusConnection<ConsensusType::Epoch>::ApplyUpdates(
    const PrePrepareMessage<ConsensusType::Epoch> & block, 
    uint8_t delegate_id)
{
    return;
}