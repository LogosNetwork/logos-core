#pragma once

#include <logos/consensus/messages/common.hpp>
#include <logos/microblock/microblock.hpp>

struct BatchStateBlock : MessageHeader<MessageType::Pre_Prepare,
                                       ConsensusType::BatchStateBlock>
{
    static const size_t HASHABLE_BYTES;

    BlockHash Hash() const;
    std::string SerializeJson() const;

    uint64_t  block_count = 0;
    BlockList blocks;
    Signature signature;
};

// Prepare and Commit messages
//
template<MessageType type, ConsensusType consensus,
         typename Enable = void
         >
struct StandardPhaseMessage;

template<MessageType type, ConsensusType consensus>
struct StandardPhaseMessage<type, consensus, typename std::enable_if<
    type == MessageType::Prepare ||
    type == MessageType::Commit>::type> : MessageHeader<type, consensus>
{
    static const size_t HASHABLE_BYTES;

    StandardPhaseMessage(uint64_t timestamp)
        : MessageHeader<type, consensus>(timestamp)
    {}

    Signature signature;
};

// Post Prepare and Post Commit messages
//
template<MessageType type, ConsensusType consensus,
         typename Enable = void
         >
struct PostPhaseMessage;

template<MessageType type, ConsensusType consensus>
struct PostPhaseMessage<type, consensus, typename std::enable_if<
    type == MessageType::Post_Prepare ||
    type == MessageType::Post_Commit>::type> : MessageHeader<type, consensus>
{
    static const size_t HASHABLE_BYTES;

    PostPhaseMessage(uint64_t timestamp)
        : MessageHeader<type, consensus>(timestamp)
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

// Convenience aliases for message names.
//
template<ConsensusType consensus>
using PrepareMessage = StandardPhaseMessage<MessageType::Prepare, consensus>;

template<ConsensusType consensus>
using CommitMessage  = StandardPhaseMessage<MessageType::Commit, consensus>;

template<ConsensusType consensus>
using PostPrepareMessage = PostPhaseMessage<MessageType::Post_Prepare, consensus>;

template<ConsensusType consensus>
using PostCommitMessage  = PostPhaseMessage<MessageType::Post_Commit, consensus>;

// Number of bytes from the beginning of the
// message that should be included in hashes
// of the message.
//
template<MessageType type, ConsensusType consensus>
const size_t PostPhaseMessage<type, consensus, typename std::enable_if<
    type == MessageType::Post_Prepare ||
    type == MessageType::Post_Commit>::type>::HASHABLE_BYTES = sizeof(PostPhaseMessage<MessageType::Post_Prepare, consensus>)
                                                               - sizeof(uint64_t)
                                                               - sizeof(AggSignature);

template<MessageType type, ConsensusType consensus>
const size_t StandardPhaseMessage<type, consensus, typename std::enable_if<
    type == MessageType::Prepare ||
    type == MessageType::Commit>::type>::HASHABLE_BYTES = sizeof(StandardPhaseMessage<MessageType::Prepare, consensus>)
                                                          - sizeof(Signature);

// Pre-Prepare Message definitions.
//
template<ConsensusType consensus_type, typename Type = void>
struct PrePrepareMessage;

template<ConsensusType consensus_type>
struct PrePrepareMessage<consensus_type,
    typename std::enable_if<
        consensus_type == ConsensusType::BatchStateBlock>::type> : BatchStateBlock
{};

template<ConsensusType consensus_type>
struct PrePrepareMessage<consensus_type,
    typename std::enable_if<
        consensus_type == ConsensusType::MicroBlock>::type> : MicroBlock
{};

// Request Message specializations. The underlying type can
// vary based on the consensus type.
//
template<ConsensusType consensus_type, typename Type = void>
struct RequestMessage;

template<ConsensusType consensus_type>
struct RequestMessage<consensus_type,
    typename std::enable_if<
        consensus_type == ConsensusType::BatchStateBlock>::type> : logos::state_block
{};

template<ConsensusType consensus_type>
struct RequestMessage<consensus_type,
    typename std::enable_if<
        consensus_type == ConsensusType::MicroBlock>::type> : PrePrepareMessage<ConsensusType::MicroBlock>
{};
