#pragma once

#include <logos/consensus/messages/common.hpp>

enum class RejectionReason : uint8_t
{
    Clock_Drift,
    Contains_Invalid_Request,
    Bad_Signature,
    Invalid_Epoch,
    New_Epoch,

    Void
};

template<ConsensusType CT>
struct RejectionMessage
    : MessageHeader<MessageType::Rejection, CT>
{
    static const size_t HASHABLE_BYTES;

    RejectionMessage(uint64_t timestamp)
        : MessageHeader<MessageType::Rejection,
              CT>(timestamp)
        , reason(RejectionReason::Void)
    {}

    RejectionReason reason;
    RejectionMap    rejection_map;
    Signature       signature;
};

template<ConsensusType CT>
const size_t RejectionMessage<CT>::HASHABLE_BYTES = sizeof(RejectionMessage<CT>)
                                                    - sizeof(Signature);
