#pragma once

#include <type_traits>
#include <cstdint>
#include <array>

#include <blake2/blake2.h>

enum class MessageType : uint8_t
{
    // Consensus
    Pre_Prepare  = 0,
    Prepare      = 1,
    Post_Prepare = 2,
    Commit       = 3,
    Post_Commit  = 4,

    // Advertisements
    Key_Advert   = 5,

    // Invalid
    Unknown
};

static const size_t CONSENSUS_HASH_SIZE     = 32;
static const size_t CONSENSUS_SIG_SIZE      = 32;
static const size_t CONSENSUS_AGG_SIG_SIZE  = 32;
static const size_t CONSENSUS_BATCH_SIZE    = 100;

using Hash         = std::array<uint8_t, CONSENSUS_HASH_SIZE>;
using Signature    = std::array<uint8_t, CONSENSUS_SIG_SIZE>;
using AggSignature = std::array<uint8_t, CONSENSUS_AGG_SIG_SIZE>;

template<MessageType type_param>
struct MessagePrequel
{
    uint8_t           version;
    const MessageType type{type_param};
} __attribute__((packed));

template<MessageType type>
struct MessageHeader : MessagePrequel<type>
{
    Hash     hash;
    uint64_t timestamp;
} __attribute__((packed));

using Prequel = MessagePrequel<MessageType::Unknown>;
