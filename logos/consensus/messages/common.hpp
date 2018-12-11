#pragma once

#include <logos/lib/blocks.hpp>

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

    // Other
    Key_Advert   = 5,
    Rejection    = 6,
    Heart_Beat   = 7,

    // Invalid
    Unknown      = 0xff
};

/// To implement a new type of consensus :
/// - define consensus type in consensus/messages/common.hpp - add new consensus type before Any
///   and update NumberOfConsensus
/// - add PrePrepareMessage and Request message for specific consensus in messages/messages.hpp (enf of file)
/// - add newconsensus type folder in consensus
/// - implement newconsensus_consensus_connection.cpp, and newconsensus_consensus_manager.[ch]pp
/// - explicitly instanciate newconsensus consensus connection in consensus_connection.cpp (end of file)
/// - explicitly instanciate newconsensus consensus manager in consensus_manager.cpp (end of file)
/// - explicitly instanciate newconsensus function in primary_delegate.cpp (top of file)
/// - update ConsensusToName in messages/util.hpp
/// - add new files to CMakeLists.txt
#define CONSENSUS_TYPE(...) \
  struct ConsensusType_Size { int __VA_ARGS__; }; \
  enum class ConsensusType:uint8_t { __VA_ARGS__,Any=0xff}; \
  static constexpr size_t CONSENSUS_TYPE_COUNT = (sizeof(ConsensusType_Size)/sizeof(int));

// Add new consensus types at the end
CONSENSUS_TYPE
(
    BatchStateBlock,
    MicroBlock,
    Epoch
);

static const size_t NUM_DELEGATES          = 32;
static const size_t CONSENSUS_HASH_SIZE    = 32;
static const size_t CONSENSUS_SIG_SIZE     = 32;
static const size_t CONSENSUS_AGG_SIG_SIZE = 32;
static const size_t CONSENSUS_PUB_KEY_SIZE = 64;
static const size_t CONSENSUS_BATCH_SIZE   = 1500;

using Signature    = std::array<uint8_t, CONSENSUS_SIG_SIZE>;
using AggSignature = std::array<uint8_t, CONSENSUS_AGG_SIG_SIZE>;
using PublicKey    = std::array<uint8_t, CONSENSUS_PUB_KEY_SIZE>;

using BlockList       = logos::state_block [CONSENSUS_BATCH_SIZE];
using BlockHash       = logos::block_hash;
using ParicipationMap = std::bitset<NUM_DELEGATES>;
using RejectionMap    = std::bitset<CONSENSUS_BATCH_SIZE>;

inline uint64_t GetStamp()
{
    using namespace std::chrono;

    return duration_cast< milliseconds >(
                system_clock::now().time_since_epoch()).count();
}

template<MessageType MT, ConsensusType CT>
struct MessagePrequel
{
    static const size_t STREAM_SIZE = sizeof(uint8_t) +
                                      sizeof(MessageType) +
                                      sizeof(ConsensusType) +
                                      sizeof(size_t);

    MessagePrequel() = default;

    MessagePrequel(bool & error, logos::stream & stream)
    {
        error = logos::read(stream, const_cast<uint8_t &>(version));
        if(error)
        {
            return;
        }

        error = logos::read(stream, const_cast<MessageType &>(type));
        if(error)
        {
            return;
        }

        error = logos::read(stream, const_cast<ConsensusType &>(consensus_type));
        if(error)
        {
            return;
        }

        error = logos::read(stream, payload_stream_size);
        if(error)
        {
            return;
        }
    }

    MessagePrequel<MT, CT> & operator= (const MessagePrequel<MT, CT> & other)
    {
        const_cast<uint8_t &>(version) = other.version;
        const_cast<size_t &>(payload_stream_size) = other.payload_stream_size;

        return *this;
    }

    void Hash(blake2b_state & hash) const
    {
        blake2b_update(&hash, &version, sizeof(version));
        blake2b_update(&hash, &type, sizeof(type));
        blake2b_update(&hash, &consensus_type, sizeof(consensus_type));
    }

    void Serialize(logos::stream & stream) const
    {
        logos::write(stream, version);
        logos::write(stream, type);
        logos::write(stream, consensus_type);
        logos::write(stream, payload_stream_size);
    }

    const uint8_t       version        = 0;
    const MessageType   type           = MT;
    const ConsensusType consensus_type = CT;
    mutable size_t payload_stream_size = 0;
};

template<MessageType MT, ConsensusType CT>
struct MessageHeader : MessagePrequel<MT, CT>
{
    static const size_t STREAM_SIZE = sizeof(uint64_t) +
                                      sizeof(BlockHash);

    MessageHeader(uint64_t timestamp)
        : timestamp(timestamp)
    {}

    MessageHeader()
        : timestamp(GetStamp())
    {}

    MessageHeader(bool & error, logos::stream & stream)
        : MessagePrequel<MT, CT>(error, stream)
    {
        if(error)
        {
            return;
        }

        error = logos::read(stream, timestamp);
        if(error)
        {
            return;
        }

        error = logos::read(stream, previous);
        if(error)
        {
            return;
        }
    }

    MessageHeader<MT, CT> & operator= (const MessageHeader<MT, CT> & other)
    {
        MessagePrequel<MT, CT>::operator=(other);

        timestamp = other.timestamp;
        previous = other.previous;

        return *this;
    }

    void Hash(blake2b_state & hash) const
    {
        MessagePrequel<MT, CT>::Hash(hash);

        blake2b_update(&hash, &timestamp, sizeof(timestamp));
        blake2b_update(&hash, &previous, sizeof(previous));
    }

    void Serialize(logos::stream & stream) const
    {
        MessagePrequel<MT, CT>::Serialize(stream);

        logos::write(stream, timestamp);
        logos::write(stream, previous);
    }

    uint64_t  timestamp;
    BlockHash previous;
};

using Prequel = MessagePrequel<MessageType::Unknown, ConsensusType::Any>;
