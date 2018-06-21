#pragma once

#include <rai/consensus/messages/common.hpp>
#include <rai/lib/blocks.hpp>

using BlockList = std::array<rai::state_block, CONSENSUS_BATCH_SIZE>;

struct BatchStateBlock : MessagePrequel<MessageType::Pre_Prepare>
{
    uint8_t   block_count;
    //BlockList blocks;
    Signature signature;
} __attribute__((packed));

template<MessageType type, typename Enable = void>
struct PostPhaseMessage;

template<MessageType type>
struct PostPhaseMessage<type, typename std::enable_if<
    type == MessageType::Post_Prepare ||
    type == MessageType::Post_Commit>::type> : MessageHeader<type>
{
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
    Signature signature;
} __attribute__((packed));

using PrePrepareMessage = BatchStateBlock;

using PrepareMessage = StandardPhaseMessage<MessageType::Prepare>;
using CommitMessage  = StandardPhaseMessage<MessageType::Commit>;

using PostPrepareMessage = PostPhaseMessage<MessageType::Post_Prepare>;
using PostCommitMessage  = PostPhaseMessage<MessageType::Post_Commit>;
