// @file
// Declares ConsensusMsgConsumer class
//
#pragma once
#include <logos/consensus/messages/common.hpp>

#include <memory>

class ConsensusMsgConsumer {
public:
    ConsensusMsgConsumer() = default;
    virtual ~ConsensusMsgConsumer() = default;

    virtual std::shared_ptr<MessageBase> Parse(const uint8_t * data,
          uint8_t version,
          MessageType message_type,
          ConsensusType consensus_type,
          uint32_t payload_size) = 0;
    virtual void OnMessage(std::shared_ptr<MessageBase> msg, MessageType message_type, bool is_p2p) = 0;

};

