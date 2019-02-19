//
// Created by gregt on 2/13/19.
//
#pragma once
#include <logos/consensus/messages/common.hpp>

class ConsensusMsgProducer {
public:
    ConsensusMsgProducer() = default;
    virtual ~ConsensusMsgProducer() = default;
    virtual bool AddToConsensusQueue(const uint8_t * data,
                                     uint8_t version,
                                     MessageType message_type,
                                     ConsensusType consensus_type,
                                     uint32_t payload_size,
                                     uint8_t delegate_id=0xff) = 0;
};

