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
        , pre_prepare_hash(pre_prepare_hash)
        , reason(RejectionReason::Void)
    {}

    RejectionMessage(bool & error, logos::stream & stream, uint8_t version)
    : MessagePrequel<MessageType::Rejection, CT>(version)
    {
        if(error)
        {
            return;
        }

        error = logos::read(stream, pre_prepare_hash);
        if(error)
        {
            return;
        }

        error = logos::read(stream, reason);
        if(error)
        {
            return;
        }


        char buf[CONSENSUS_BATCH_SIZE];
        error = logos::read(stream, buf);
        if(error)
        {
            return;
        }
        std::string s(buf, CONSENSUS_BATCH_SIZE);
        new (&rejection_map) RejectionMap(s);

        error = logos::read(stream, signature);
        if(error)
        {
            return;
        }
    }

    BlockHash Hash() const
    {
        return Blake2bHash<RejectionMessage<CT>>(*this);
    }

    void Hash(blake2b_state & hash) const
    {
        MessagePrequel<MessageType::Rejection, CT>::Hash(hash);
        pre_prepare_hash.Hash(hash);
        blake2b_update(&hash, &reason, sizeof(uint8_t));
        auto s = rejection_map.to_string();
        blake2b_update(&hash, s.data(), s.length());
    }


    uint32_t Serialize(logos::stream & stream) const
    {
        auto s = logos::write(stream, pre_prepare_hash);
        s += logos::write(stream, reason);

        //TODO serialized space
        char buf[CONSENSUS_BATCH_SIZE];
        std::string str = rejection_map.to_string();
        assert(str.size()==CONSENSUS_BATCH_SIZE);
        memcpy(buf, str.data(), str.size());

        s += logos::write(stream, buf);
        s += logos::write(stream, signature);
        return s;
    }

    void Serialize(std::vector<uint8_t> & t) const
    {
        {
            logos::vectorstream stream(t);
            MessagePrequel<MessageType::Rejection, CT>::Serialize(stream);
            MessagePrequel<MessageType::Rejection, CT>::payload_size = htole32(Serialize(stream));
        }
        {
            logos::vectorstream header_stream(t);
            MessagePrequel<MessageType::Rejection, CT>::Serialize(header_stream);
        }
    }

    BlockHash           pre_prepare_hash;
    RejectionReason     reason;
    RejectionMap        rejection_map;
    DelegateSig         signature;
};
