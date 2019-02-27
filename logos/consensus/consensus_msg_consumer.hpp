// @file
// Declares ConsensusMsgConsumer class
//
#pragma once
#include <logos/consensus/messages/common.hpp>

#include <memory>

/// Declares consumer interface
class ConsensusMsgConsumer {
public:
    /// Class constructor
    ConsensusMsgConsumer() = default;
    /// Class destructor
    virtual ~ConsensusMsgConsumer() = default;

    /// Deserialize the message
    /// @param data buffer
    /// @param version logos protocol version
    /// @param message_type consensus message type
    /// @param consensus_type consensus type
    /// @param payload_size buffer size
    /// @returns base pointer to consensus message
    virtual std::shared_ptr<MessageBase> Parse(const uint8_t * data,
                                               uint8_t version,
                                               MessageType message_type,
                                               ConsensusType consensus_type,
                                               uint32_t payload_size) = 0;
    /// Handle consensus message
    /// @param msg consensus message
    /// @param message_type consensus message type
    /// @param is_p2p true if message is received via p2p
    virtual void OnMessage(std::shared_ptr<MessageBase> msg, MessageType message_type, bool is_p2p) = 0;

};

