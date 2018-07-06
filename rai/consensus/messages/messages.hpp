#pragma once

#include <rai/consensus/messages/common.hpp>

struct BatchStateBlock : MessagePrequel<MessageType::Pre_Prepare>
{
    BlockHash Hash() const
    {
        rai::uint256_union result;
        blake2b_state hash;

        auto status (blake2b_init (&hash, sizeof (result.bytes)));
        assert (status == 0);

        blake2b_update(&hash, blocks, sizeof(BlockList));

        status = blake2b_final (&hash, result.bytes.data (), sizeof (result.bytes));
        assert (status == 0);

        return result;
    }

    uint8_t   block_count = 0;
    BlockList blocks;
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

    uint64_t     participation_map;
    AggSignature signature;
} __attribute__((packed));

template<MessageType type, typename Enable = void>
struct StandardPhaseMessage;

template<MessageType type>
struct StandardPhaseMessage<type, typename std::enable_if<
    type == MessageType::Prepare ||
    type == MessageType::Commit>::type> : MessageHeader<type>
{
    static const size_t HASHABLE_BYTES;

    Signature signature;
} __attribute__((packed));

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
