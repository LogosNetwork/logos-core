#pragma once

#include <logos/consensus/messages/common.hpp>
#include <logos/epoch/epoch_transition.hpp>
#include <logos/microblock/microblock.hpp>
#include <logos/epoch/epoch.hpp>

struct BatchStateBlock : MessageHeader<MessageType::Pre_Prepare,
                                       ConsensusType::BatchStateBlock>
{
    static const size_t HASHABLE_BYTES;

    BatchStateBlock & operator= (const BatchStateBlock & other)
    {
        auto b_size = other.block_count * sizeof(logos::state_block);

        // BatchStateBlock members
        sequence = other.sequence;
        block_count = other.block_count;
        memcpy(blocks, other.blocks, b_size);
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

    uint64_t  sequence;
    uint64_t  block_count = 0;
    uint32_t  epoch_number = 0;
    uint32_t  padding = 0;
    BlockList blocks;
    BlockHash next;
    Signature signature;
};

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
    static const size_t HASHABLE_BYTES;

    StandardPhaseMessage(uint64_t timestamp)
        : MessageHeader<MT, CT>(timestamp)
    {}

    Signature signature;
};

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
    static const size_t HASHABLE_BYTES;

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

// Number of bytes from the beginning of the
// message that should be included in hashes
// of the message.
//
template<MessageType MT, ConsensusType CT>
const size_t PostPhaseMessage<MT, CT, typename std::enable_if<
    MT == MessageType::Post_Prepare ||
    MT == MessageType::Post_Commit>::type>::HASHABLE_BYTES = sizeof(PostPhaseMessage<MessageType::Post_Prepare, CT>)
                                                             - sizeof(uint64_t)
                                                             - sizeof(AggSignature);

template<MessageType MT, ConsensusType CT>
const size_t StandardPhaseMessage<MT, CT, typename std::enable_if<
    MT == MessageType::Prepare ||
    MT == MessageType::Commit>::type>::HASHABLE_BYTES = sizeof(StandardPhaseMessage<MessageType::Prepare, CT>)
                                                        - sizeof(Signature);

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
