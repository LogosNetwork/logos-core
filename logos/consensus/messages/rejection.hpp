#pragma once

#include <logos/consensus/messages/common.hpp>
#include <logos/node/utility.hpp>

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
    : MessagePrequel<MessageType::Rejection, CT>
{

    RejectionMessage(const BlockHash & pre_prepare_hash)
        : MessagePrequel<MessageType::Rejection, CT>()
        , preprepare_hash(pre_prepare_hash)
        , reason(RejectionReason::Void)
    {}

    RejectionMessage(bool & error, logos::stream & stream, uint8_t version)
    : MessagePrequel<MessageType::Rejection, CT>(version)
    {
        error = logos::read(stream, preprepare_hash);
        if(error)
        {
            return;
        }

        error = logos::read(stream, reason);
        if(error)
        {
            return;
        }

        error = logos::read(stream, rejection_map);
        if(error)
        {
            return;
        }

        error = logos::read(stream, signature);
    }

    BlockHash Hash() const
    {
        return Blake2bHash<RejectionMessage<CT>>(*this);
    }

    void Hash(blake2b_state & hash) const
    {
        MessagePrequel<MessageType::Rejection, CT>::Hash(hash);
        preprepare_hash.Hash(hash);
        blake2b_update(&hash, &reason, sizeof(uint8_t));

        std::vector<uint8_t> buf;
        {
            logos::vectorstream stream(buf);
            logos::write(stream, rejection_map);
        }
        blake2b_update(&hash, buf.data(), buf.size());
    }


    uint32_t Serialize(logos::stream & stream) const
    {
        auto s = MessagePrequel<MessageType::Rejection, CT>::Serialize(stream);
        s += logos::write(stream, preprepare_hash);
        s += logos::write(stream, reason);
        s += logos::write(stream, rejection_map);
        s += logos::write(stream, signature);
        return s;
    }

    void Serialize(std::vector<uint8_t> & buf) const
    {
        assert(buf.empty());
        {
            logos::vectorstream stream(buf);
            MessagePrequel<MessageType::Rejection, CT>::payload_size = Serialize(stream)
                    - MessagePrequelSize;
        }
        {
            HeaderStream header_stream(buf.data(), MessagePrequelSize);
            MessagePrequel<MessageType::Rejection, CT>::Serialize(header_stream);
        }
    }

    BlockHash           preprepare_hash;
    RejectionReason     reason;
    RejectionMap        rejection_map;
    DelegateSig         signature;
};
