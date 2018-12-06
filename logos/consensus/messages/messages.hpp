#pragma once

#include <logos/consensus/messages/common.hpp>
#include <logos/epoch/epoch_transition.hpp>
#include <logos/microblock/microblock.hpp>
#include <logos/epoch/epoch.hpp>
#include <logos/lib/blocks.hpp>

#include <logos/lib/log.hpp>

struct BatchStateBlock : MessageHeader<MessageType::Pre_Prepare,
                                       ConsensusType::BatchStateBlock>
{
    using Header = MessageHeader<MessageType::Pre_Prepare,
                                 ConsensusType::BatchStateBlock>;
    using Prequel = MessagePrequel<MessageType::Pre_Prepare,
                                   ConsensusType::BatchStateBlock>;

    static const size_t STREAM_SIZE = sizeof(uint64_t) +
                                      sizeof(uint64_t) +
                                      sizeof(uint32_t) +
                                      sizeof(BlockHash) +
                                      sizeof(Signature);

    BatchStateBlock() = default;
    BatchStateBlock(bool & error, logos::stream & stream);

    BatchStateBlock & operator= (const BatchStateBlock & other)
    {
        auto b_size = other.block_count * sizeof(logos::state_block);

        // BatchStateBlock members
        sequence = other.sequence;
        block_count = other.block_count;
        epoch_number = other.epoch_number;
        for(uint64_t i = 0; i < block_count; ++i)
        {
            new(&blocks[i]) logos::state_block(other.blocks[i]);
        }
        next = other.next;
        memcpy(signature.data(), other.signature.data(), sizeof(signature));

        // MessageHeader members
        timestamp = other.timestamp;
        previous = other.previous;

        // MessagePrequel members are
        // unchanged.

        return *this;
    }

    BlockHash Hash() const;
    std::string SerializeJson() const;
    void SerializeJson(boost::property_tree::ptree &) const;
    void Serialize(logos::stream & stream) const;

    uint64_t  sequence     = 0;
    uint64_t  block_count  = 0;
    uint32_t  epoch_number = 0;
    BlockList blocks;
    BlockHash next;
    Signature signature;
};

std::ostream& operator<<(std::ostream& os, const BatchStateBlock& b);


// Prepare and Commit messages
//
template<MessageType MT, ConsensusType CT,
         typename E = void
         >
struct StandardPhaseMessage;

template<MessageType MT, ConsensusType CT>
struct StandardPhaseMessage<MT, CT, typename std::enable_if<
    MT == MessageType::Prepare ||
    MT == MessageType::Commit>::type> : MessageHeader<MT, CT>
{
    StandardPhaseMessage(uint64_t timestamp)
        : MessageHeader<MT, CT>(timestamp)
    {}

    BlockHash Hash() const
    {
        logos::uint256_union result;
        blake2b_state hash;

        auto status(blake2b_init(&hash, sizeof(result.bytes)));
        assert(status == 0);

        MessageHeader<MT, CT>::Hash(hash);

        status = blake2b_final(&hash, result.bytes.data(), sizeof(result.bytes));
        assert(status == 0);

        return result;
    }

    Signature signature;
};

template<MessageType MT, ConsensusType CT>
std::ostream& operator<<(std::ostream& os, const StandardPhaseMessage<MT, CT>& m);


// Post Prepare and Post Commit messages
//
template<MessageType MT, ConsensusType CT,
         typename Enable = void
         >
struct PostPhaseMessage;

template<MessageType MT, ConsensusType CT>
struct PostPhaseMessage<MT, CT, typename std::enable_if<
    MT == MessageType::Post_Prepare ||
    MT == MessageType::Post_Commit>::type> : MessageHeader<MT, CT>
{
    PostPhaseMessage(uint64_t timestamp)
        : MessageHeader<MT, CT>(timestamp)
    {}

    ParicipationMap participation_map;
    AggSignature    signature;
};

// Key advertisement
//
struct KeyAdvertisement : MessagePrequel<MessageType::Key_Advert,
                                         ConsensusType::Any>
{
    PublicKey public_key;
};

struct ConnectedClientIds
{
    uint epoch_number;
    uint8_t delegate_id;
    EpochConnection connection;
};

// Convenience aliases for message names.
//
template<ConsensusType CT>
using PrepareMessage = StandardPhaseMessage<MessageType::Prepare, CT>;

template<ConsensusType CT>
using CommitMessage = StandardPhaseMessage<MessageType::Commit, CT>;

template<ConsensusType CT>
using PostPrepareMessage = PostPhaseMessage<MessageType::Post_Prepare, CT>;

template<ConsensusType CT>
using PostCommitMessage = PostPhaseMessage<MessageType::Post_Commit, CT>;

// Pre-Prepare Message definitions.
//
template<ConsensusType CT, typename E = void>
struct PrePrepareMessage;

template<ConsensusType CT>
struct PrePrepareMessage<CT,
    typename std::enable_if<
        CT == ConsensusType::BatchStateBlock>::type> : BatchStateBlock
{};

template<ConsensusType CT>
struct PrePrepareMessage<CT,
    typename std::enable_if<
        CT == ConsensusType::MicroBlock>::type> : MicroBlock
{};

template<ConsensusType CT>
struct PrePrepareMessage<CT,
    typename std::enable_if<
        CT == ConsensusType::Epoch>::type> : Epoch
{};

// Request Message specializations. The underlying type can
// vary based on the consensus type.
//
template<ConsensusType CT, typename Type = void>
struct RequestMessage;

template<ConsensusType CT>
struct RequestMessage<CT,
    typename std::enable_if<
        CT == ConsensusType::BatchStateBlock>::type> : logos::state_block
{};

template<ConsensusType CT>
struct RequestMessage<CT, 
	typename std::enable_if< 
		CT == ConsensusType::MicroBlock>::type> : PrePrepareMessage<ConsensusType::MicroBlock>
{};

template<ConsensusType CT>
struct RequestMessage<CT, 
	typename std::enable_if< 
		CT == ConsensusType::Epoch>::type> : PrePrepareMessage<ConsensusType::Epoch>
{};
