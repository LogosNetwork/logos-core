//
// @file
// Declares producer interface
#pragma once
#include <logos/consensus/messages/common.hpp>

/// Declares producer interface
class ConsensusMsgProducer {
public:
    /// Class constructor
    ConsensusMsgProducer() = default;
    /// Class destructor
    virtual ~ConsensusMsgProducer() = default;
    /// Add message to the queue
    /// @param data buffer
    /// @param version logos protocol version
    /// @param message_type consensus message type
    /// @param consensus_type consensus type
    /// @param payload_size buffer size
    /// @param dest_delegate_id destination delegate id
    virtual bool AddToConsensusQueue(const uint8_t * data,
                                     uint8_t version,
                                     MessageType message_type,
                                     ConsensusType consensus_type,
                                     uint32_t payload_size,
                                     uint8_t dest_delegate_id=0xff) = 0;
};
