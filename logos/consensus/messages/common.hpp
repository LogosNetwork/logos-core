#pragma once

#include <type_traits>
#include <cstdint>
#include <bitset>
#include <array>
#include <chrono>

#include <blake2/blake2.h>

#include <logos/consensus/messages/byte_arrays.hpp>
#include <boost/iostreams/stream_buffer.hpp>

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

    Post_Committed_Block, //to be stored locally and distributed to fall nodes

    // Invalid
    Unknown      = 0xff
};

static constexpr uint8_t logos_version = 0;

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
    BatchStateBlock = 0,
    MicroBlock = 1,
    Epoch = 2
);

static const size_t NUM_DELEGATES               = 32;
static const size_t CONSENSUS_BATCH_SIZE        = 1500;

using ParicipationMap       = std::bitset<NUM_DELEGATES>;
using RejectionMap          = std::vector<bool>;

inline uint64_t GetStamp()
{
    using namespace std::chrono;

    return duration_cast< milliseconds >(
                system_clock::now().time_since_epoch()).count();
}

template<typename T>
BlockHash Blake2bHash(const T & t)
{
    BlockHash digest;
    blake2b_state hash;

    auto status(blake2b_init(&hash, HASH_SIZE));
    assert(status == 0);

    t.Hash(hash);

    status = blake2b_final(&hash, digest.data(), HASH_SIZE);
    assert(status == 0);

    return digest;
}

struct AggSignature
{
    ParicipationMap     map;
    DelegateSig         sig;
    static_assert (sizeof(unsigned long)*8==64, "sizeof(unsigned long)*8!=64");

    AggSignature() = default;

    AggSignature(bool & error, logos::stream & stream);

    void Hash(blake2b_state & hash) const
    {
        unsigned long m = htole64(map.to_ulong());
        blake2b_update(&hash, &m, sizeof(m));
        sig.Hash(hash);
    }

    uint32_t Serialize(logos::stream & stream) const
    {
        unsigned long m = htole64(map.to_ulong());
        uint32_t s = logos::write(stream, m);
        s += logos::write(stream, sig);
        return s;
    }

    void SerializeJson(boost::property_tree::ptree & tree) const
    {
        tree.put("paricipation_map", map.to_string());
        tree.put("signature", sig.to_string());
    }
};

static constexpr uint32_t MessagePrequelSize = 8;

template<MessageType MT, ConsensusType CT>
struct MessagePrequel
{
    MessagePrequel(uint8_t version = logos_version)
    : version(version)
    {}

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

        error = logos::read(stream, mpf);
        if(error)
        {
            return;
        }

        error = logos::read(stream, payload_size);
        if(error)
        {
            return;
        }
        payload_size = le32toh(payload_size);
    }

    MessagePrequel<MT, CT> & operator= (const MessagePrequel<MT, CT> & other)
    {
        const_cast<uint8_t &>(version) = other.version;
        payload_size = other.payload_size;

        return *this;
    }

    void Hash(blake2b_state & hash) const
    {
        blake2b_update(&hash, &version, sizeof(uint8_t));
    }

    uint32_t Serialize(logos::stream & stream) const
    {
        auto s = logos::write(stream, version);
        s += logos::write(stream, type);
        s += logos::write(stream, consensus_type);
        s += logos::write(stream, mpf);
        s += logos::write(stream, htole32(payload_size));

        assert(s == MessagePrequelSize);
        return s;
    }

    const uint8_t       version         = logos_version;
    const MessageType   type            = MT;
    const ConsensusType consensus_type  = CT;
    uint8_t             mpf             = 0;
    mutable uint32_t    payload_size    = 0;
};

using Prequel = MessagePrequel<MessageType::Unknown, ConsensusType::Any>;

struct PrePrepareCommon
{
    PrePrepareCommon()
    : primary_delegate()
    , epoch_number(0)
    , sequence(0)
    , timestamp(GetStamp())
    , previous()
    , preprepare_sig()
    { }

    PrePrepareCommon(bool & error, logos::stream & stream);

    PrePrepareCommon & operator= (const PrePrepareCommon & other)
    {
        primary_delegate        = other.primary_delegate;
        epoch_number    = other.epoch_number;
        sequence        = other.sequence;
        timestamp       = other.timestamp;
        previous        = other.previous;
        preprepare_sig  = other.preprepare_sig;
        return *this;
    }

    void Hash(blake2b_state & hash) const
    {
        uint32_t en = htole32(epoch_number);
        uint32_t sqn = htole32(sequence);
        uint64_t tsp = htole64(timestamp);

        primary_delegate.Hash(hash);
        blake2b_update(&hash, &en, sizeof(uint32_t));
        blake2b_update(&hash, &sqn, sizeof(uint32_t));
        blake2b_update(&hash, &tsp, sizeof(uint64_t));
        previous.Hash(hash);
    }

    uint32_t Serialize(logos::stream & stream) const
    {
        uint32_t en = htole32(epoch_number);
        uint32_t sqn = htole32(sequence);
        uint64_t tsp = htole64(timestamp);

        auto s = logos::write(stream, primary_delegate);
        s += logos::write(stream, en);
        s += logos::write(stream, sqn);
        s += logos::write(stream, tsp);
        s += logos::write(stream, previous);
        s += logos::write(stream, preprepare_sig);

        return s;
    }

    void SerializeJson(boost::property_tree::ptree & tree) const
    {
        tree.put("delegate", primary_delegate.to_string());
        tree.put("epoch_number", std::to_string(epoch_number));
        tree.put("sequence", std::to_string(sequence));
        tree.put("timestamp", std::to_string(timestamp));
        tree.put("previous", previous.to_string());
        tree.put("signature", preprepare_sig.to_string());
    }

    AccountAddress          primary_delegate;
    uint32_t                epoch_number;
    uint32_t                sequence;
    uint64_t                timestamp;
    BlockHash               previous;
    DelegateSig             preprepare_sig;
};

using HeaderStream = boost::iostreams::stream_buffer<boost::iostreams::basic_array_sink<uint8_t>>;

