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
/// - add new files to CMakeLists.txt
////////////////////
// Consensus type has to be sequential
 // because it is also an index.
enum class ConsensusType : uint8_t
{
    BatchStateBlock = 0,
    MicroBlock = 1,
    Epoch = 2,
    Any = 100 /// keep it 100 so adding a new type would not change compatibility with any
};
 // The number of consensus excluding Any
const uint8_t NumberOfConsensus = 3;

static const size_t NUM_DELEGATES           = 32;
static const size_t CONSENSUS_HASH_SIZE     = 32;
static const size_t CONSENSUS_SIG_SIZE      = 32;
static const size_t CONSENSUS_AGG_SIG_SIZE  = 32;
static const size_t CONSENSUS_PUB_KEY_SIZE  = 64;
static const size_t CONSENSUS_BATCH_SIZE    = 1500;

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

template<MessageType type_param, ConsensusType consensus_param>
struct MessagePrequel
{
	static const size_t PADDING_SIZE = 5;
    const uint8_t     version = 0;
    const MessageType type = type_param;
    const ConsensusType consensus_type = consensus_param;
	const uint8_t     pad[PADDING_SIZE] = {0,0,0,0,0}; // FIXME Do not use manual padding
};

template<MessageType type, ConsensusType consensus>
struct MessageHeader : MessagePrequel<type, consensus>
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

using Prequel = MessagePrequel<MessageType::Unknown, ConsensusType::Any>;
