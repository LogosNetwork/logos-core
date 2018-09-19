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

    // Advertisements
    Key_Advert   = 5,

    // Invalid
    Unknown
};
////////////////////
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
////////////////////
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

inline uint64_t GetStamp()
{
    using namespace std::chrono;

    return duration_cast< milliseconds >(
                system_clock::now().time_since_epoch()).count();
}

template<MessageType MT, ConsensusType CT>
struct MessagePrequel
{
    static const size_t PADDING_SIZE = 5;

    const uint8_t       version           = 0;
    const MessageType   type              = MT;
    const ConsensusType consensus_type    = CT;
    const uint8_t       pad[PADDING_SIZE] = {0,0,0,0,0}; // FIXME Do not use manual padding
};

template<MessageType MT, ConsensusType CT>
struct MessageHeader : MessagePrequel<MT, CT>
{
    MessageHeader(uint64_t timestamp)
        : timestamp(timestamp)
    {}

    MessageHeader()
        : timestamp(GetStamp())
    {}

    uint64_t  timestamp;
    BlockHash previous;
};

using Prequel = MessagePrequel<MessageType::Unknown, ConsensusType::Any>;
