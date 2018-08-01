#pragma once

#include <rai/lib/blocks.hpp>

#include <type_traits>
#include <cstdint>
#include <bitset>
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

static const size_t NUM_DELEGATES           = 32;
static const size_t CONSENSUS_HASH_SIZE     = 32;
static const size_t CONSENSUS_SIG_SIZE      = 32;
static const size_t CONSENSUS_AGG_SIG_SIZE  = 32;
static const size_t CONSENSUS_PUB_KEY_SIZE  = 64;
static const size_t CONSENSUS_BATCH_SIZE    = 1500;

using Signature    = std::array<uint8_t, CONSENSUS_SIG_SIZE>;
using AggSignature = std::array<uint8_t, CONSENSUS_AGG_SIG_SIZE>;
using PublicKey    = std::array<uint8_t, CONSENSUS_PUB_KEY_SIZE>;

using BlockList       = rai::state_block [CONSENSUS_BATCH_SIZE];
using BlockHash       = rai::block_hash;
using ParicipationMap = std::bitset<NUM_DELEGATES>;

inline uint64_t GetStamp()
{
    using namespace std::chrono;

    return duration_cast< milliseconds >(
                system_clock::now().time_since_epoch()).count();
}

template<MessageType type_param>
struct MessagePrequel
{
    const uint8_t     version = 0;
    const MessageType type = type_param;
};

template<MessageType type>
struct MessageHeader : MessagePrequel<type>
{
    MessageHeader(uint64_t timestamp)
        : timestamp(timestamp)
    {}

    MessageHeader()
        : timestamp(GetStamp())
    {}

    uint64_t  timestamp;
    BlockHash hash;
};

using Prequel = MessagePrequel<MessageType::Unknown>;
