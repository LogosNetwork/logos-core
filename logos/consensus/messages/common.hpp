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

    Post_Committed_Block = 8, //to be stored locally and distributed to fall nodes

    TxAcceptor_Message   = 9,

    // Invalid
    Unknown      = 0xff
};

static constexpr uint8_t logos_version = 0;

/// To implement a new type of consensus :
/// - define new consensus type in consensus/messages/common.hpp
/// - add PrePrepareMessage and Request message for new consensus type in messages/messages.hpp (end of file)
/// - add new consensus type folder in logos/consensus/
/// - implement new consensus_backup_delegate.cpp, and new consensus_consensus_manager.[ch]pp
/// - explicitly instantiate new consensus BackupDelegate in backup_delegate.cpp (end of file)
/// - explicitly instantiate new consensus ConsensusManager in consensus_manager.cpp (end of file)
/// - explicitly instantiate new consensus function in primary_delegate.cpp (top of file)
/// - update ConsensusToName in messages/util.hpp
/// - add new files to CMakeLists.txt
#define CONSENSUS_TYPE(...)            \
    struct ConsensusType_Size          \
    {                                  \
        int __VA_ARGS__;               \
    } __attribute__((packed));         \
    enum class ConsensusType : uint8_t \
    {                                  \
        __VA_ARGS__,                   \
        Any = 0xff                     \
    };                                 \
    static constexpr size_t CONSENSUS_TYPE_COUNT = (sizeof(ConsensusType_Size)/sizeof(int));

// Add new consensus types at the end
CONSENSUS_TYPE
(
    Request    = 0,
    MicroBlock = 1,
    Epoch      = 2
);

static const size_t NUM_DELEGATES        = 32;
static const size_t CONSENSUS_BATCH_SIZE = 1500;

using BatchTips       = BlockHash[NUM_DELEGATES];
using ParicipationMap = std::bitset<NUM_DELEGATES>;
using RejectionMap    = std::vector<bool>;

inline uint64_t GetStamp()
{
    using namespace std::chrono;

    return duration_cast< milliseconds >(
                system_clock::now().time_since_epoch()).count();
}

struct AggSignature
{
    static_assert (sizeof(unsigned long)*8==64,
                   "sizeof(unsigned long)*8!=64");

    AggSignature() = default;

    AggSignature(bool & error, logos::stream & stream);

    void Hash(blake2b_state & hash) const;

    uint32_t Serialize(logos::stream & stream) const;
    void SerializeJson(boost::property_tree::ptree & tree) const;
    void clear();

    bool operator== (AggSignature const &) const;
    bool operator!= (AggSignature const &) const;

    ParicipationMap map;
    DelegateSig     sig;
};


struct MessageBase {
    MessageBase() = default;
    virtual ~MessageBase() = default;
};

static constexpr uint32_t MessagePrequelSize = 8;

template<MessageType MT, ConsensusType CT>
struct MessagePrequel : public MessageBase
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

    const uint8_t       version        = logos_version;
    const MessageType   type           = MT;
    const ConsensusType consensus_type = CT;
    uint8_t             mpf            = 0;
    mutable uint32_t    payload_size   = 0;
};

using Prequel = MessagePrequel<MessageType::Unknown, ConsensusType::Any>;

struct PrePrepareCommon
{
    PrePrepareCommon();

    PrePrepareCommon(bool & error, logos::stream & stream);

    PrePrepareCommon & operator= (const PrePrepareCommon & other);

    virtual void Hash(blake2b_state & hash, bool is_archive_block = false) const;

    uint32_t Serialize(logos::stream & stream) const;
    void SerializeJson(boost::property_tree::ptree & tree) const;

    uint8_t     primary_delegate;
    uint32_t    epoch_number;
    uint32_t    sequence;
    uint64_t    timestamp;
    mutable BlockHash   previous;
    DelegateSig preprepare_sig;
};

using HeaderStream = boost::iostreams::stream_buffer<boost::iostreams::basic_array_sink<uint8_t>>;

