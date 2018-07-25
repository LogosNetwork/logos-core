#pragma once

#include <rai/consensus/messages/common.hpp>

struct BatchStateBlock : MessageHeader<MessageType::Pre_Prepare>
{
    static const size_t HASHABLE_BYTES;

    BlockHash Hash() const;

    uint64_t  block_count = 0;
    BlockList blocks;
    Signature signature;
};

template<MessageType type, typename Enable = void>
struct StandardPhaseMessage;

template<MessageType type>
struct StandardPhaseMessage<type, typename std::enable_if<
    type == MessageType::Prepare ||
    type == MessageType::Commit>::type> : MessageHeader<type>
{
    static const size_t HASHABLE_BYTES;

    StandardPhaseMessage(uint64_t timestamp)
        : MessageHeader<type>(timestamp)
    {}

    Signature signature;
};

template<MessageType type, typename Enable = void>
struct PostPhaseMessage;

template<MessageType type>
struct PostPhaseMessage<type, typename std::enable_if<
    type == MessageType::Post_Prepare ||
    type == MessageType::Post_Commit>::type> : MessageHeader<type>
{
    static const size_t HASHABLE_BYTES;

    PostPhaseMessage(uint64_t timestamp)
        : MessageHeader<type>(timestamp)
    {}

    ParicipationMap participation_map;
    AggSignature    signature;
};

struct KeyAdvertisement : MessagePrequel<MessageType::Key_Advert>
{
    PublicKey public_key;
};

using PrePrepareMessage = BatchStateBlock;

using PrepareMessage = StandardPhaseMessage<MessageType::Prepare>;
using CommitMessage  = StandardPhaseMessage<MessageType::Commit>;

using PostPrepareMessage = PostPhaseMessage<MessageType::Post_Prepare>;
using PostCommitMessage  = PostPhaseMessage<MessageType::Post_Commit>;


template<MessageType type>
const size_t PostPhaseMessage<type, typename std::enable_if<
    type == MessageType::Post_Prepare ||
    type == MessageType::Post_Commit>::type>::HASHABLE_BYTES = sizeof(PostPhaseMessage<MessageType::Post_Prepare>)
                                                               - sizeof(uint64_t)
                                                               - sizeof(AggSignature);

template<MessageType type>
const size_t StandardPhaseMessage<type, typename std::enable_if<
    type == MessageType::Prepare ||
    type == MessageType::Commit>::type>::HASHABLE_BYTES = sizeof(StandardPhaseMessage<MessageType::Prepare>)
                                                          - sizeof(Signature);
