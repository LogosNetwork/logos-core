#pragma once

#include <logos/consensus/messages/common.hpp>

enum class RejectionReason : uint8_t
{
    Clock_Drift,
    Contains_Invalid_Request,
    Bad_Signature,
    Invalid_Previous_Hash,
    Wrong_Sequence_Number,
    Invalid_Epoch,
    New_Epoch,

    Void
};

template<ConsensusType CT>
struct RejectionMessage
    : MessageHeader<MessageType::Rejection, CT>
{

    RejectionMessage(uint64_t timestamp)
        : MessageHeader<MessageType::Rejection,
              CT>(timestamp)
        , reason(RejectionReason::Void)
    {}

    BlockHash Hash() const
    {
        logos::uint256_union result;
        blake2b_state hash;

        auto status(blake2b_init(&hash, sizeof(result.bytes)));
        assert(status == 0);

        MessageHeader<MessageType::Rejection, CT>::Hash(hash);

        status = blake2b_final(&hash, result.bytes.data(), sizeof(result.bytes));
        assert(status == 0);

        return result;
    }

    RejectionReason reason;
    RejectionMap    rejection_map;
    Signature       signature;
};
