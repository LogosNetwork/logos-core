#pragma once

#include <logos/common.hpp>
#include <logos/lib/interface.h>
#include <logos/consensus/messages/common.hpp>

#include <boost/asio.hpp>

#include <bitset>

#include <xxhash/xxhash.h>

namespace logos
{
using endpoint = boost::asio::ip::udp::endpoint;
bool parse_port (std::string const &, uint16_t &);
bool parse_address_port (std::string const &, boost::asio::ip::address &, uint16_t &);
using tcp_endpoint = boost::asio::ip::tcp::endpoint;
bool parse_endpoint (std::string const &, logos::endpoint &);
bool parse_tcp_endpoint (std::string const &, logos::tcp_endpoint &);
bool reserved_address (logos::endpoint const &);
}
static uint64_t endpoint_hash_raw (logos::endpoint const & endpoint_a)
{
    assert (endpoint_a.address ().is_v6 ());
    logos::uint128_union address;
    address.bytes = endpoint_a.address ().to_v6 ().to_bytes ();
    XXH64_state_t hash;
    XXH64_reset (&hash, 0);
    XXH64_update (&hash, address.bytes.data (), address.bytes.size ());
    auto port (endpoint_a.port ());
    XXH64_update (&hash, &port, sizeof (port));
    auto result (XXH64_digest (&hash));
    return result;
}

namespace std
{
template <size_t size>
struct endpoint_hash
{
};
template <>
struct endpoint_hash<8>
{
    size_t operator() (logos::endpoint const & endpoint_a) const
    {
        return endpoint_hash_raw (endpoint_a);
    }
};
template <>
struct endpoint_hash<4>
{
    size_t operator() (logos::endpoint const & endpoint_a) const
    {
        uint64_t big (endpoint_hash_raw (endpoint_a));
        uint32_t result (static_cast<uint32_t> (big) ^ static_cast<uint32_t> (big >> 32));
        return result;
    }
};
template <>
struct hash<logos::endpoint>
{
    size_t operator() (logos::endpoint const & endpoint_a) const
    {
        endpoint_hash<sizeof (size_t)> ehash;
        return ehash (endpoint_a);
    }
};
}
namespace boost
{
template <>
struct hash<logos::endpoint>
{
    size_t operator() (logos::endpoint const & endpoint_a) const
    {
        std::hash<logos::endpoint> hash;
        return hash (endpoint_a);
    }
};
}

namespace logos
{
enum class message_type : uint8_t
{
    invalid, // 0
    not_a_type, // 1
    keepalive, // 2
    publish, // 3
    confirm_req, // 4
    confirm_ack, // 5
    bulk_pull, // 6
    bulk_push, // 7
    frontier_req, // 8
    bulk_pull_blocks, // 9
    batch_blocks_pull // 10
};
enum class bulk_pull_blocks_mode : uint8_t
{
    list_blocks,
    checksum_blocks
};
class message_visitor;
class message
{
public:
    message (logos::message_type);
    message (bool &, logos::stream &);
    virtual ~message () = default;
    void write_header (logos::stream &);
    static bool read_header (logos::stream &, uint8_t &, uint8_t &, uint8_t &, logos::message_type &, std::bitset<16> &);
    virtual void serialize (logos::stream &) = 0;
    virtual bool deserialize (logos::stream &) = 0;
    virtual void visit (logos::message_visitor &) const = 0;
    logos::block_type block_type () const;
    void block_type_set (logos::block_type);
    bool ipv4_only ();
    void ipv4_only_set (bool);
    static std::array<uint8_t, 2> constexpr magic_number = logos::logos_network ==logos::logos_networks::logos_test_network ? std::array<uint8_t, 2> ({ 'R', 'A' }) : logos::logos_network ==logos::logos_networks::logos_beta_network ? std::array<uint8_t, 2> ({ 'R', 'B' }) : std::array<uint8_t, 2> ({ 'R', 'C' });
    uint8_t version_max;
    uint8_t version_using;
    uint8_t version_min;
    logos::message_type type;
    std::bitset<16> extensions;
    static size_t constexpr ipv4_only_position = 1;
    static size_t constexpr bootstrap_server_position = 2;
    static std::bitset<16> constexpr block_type_mask = std::bitset<16> (0x0f00);
};
class work_pool;
class message_parser
{
public:
    enum class parse_status
    {
        success,
        insufficient_work,
        invalid_header,
        invalid_message_type,
        invalid_keepalive_message,
        invalid_publish_message,
        invalid_confirm_req_message,
        invalid_confirm_ack_message
    };
    message_parser (logos::message_visitor &, logos::work_pool &);
    void deserialize_buffer (uint8_t const *, size_t);
    void deserialize_keepalive (uint8_t const *, size_t);
    void deserialize_publish (uint8_t const *, size_t);
    void deserialize_confirm_req (uint8_t const *, size_t);
    void deserialize_confirm_ack (uint8_t const *, size_t);
    bool at_end (logos::bufferstream &);
    logos::message_visitor & visitor;
    logos::work_pool & pool;
    parse_status status;
};
class keepalive : public message
{
public:
    keepalive ();
    void visit (logos::message_visitor &) const override;
    bool deserialize (logos::stream &) override;
    void serialize (logos::stream &) override;
    bool operator== (logos::keepalive const &) const;
    std::array<logos::endpoint, 8> peers;
};
class frontier_req : public message
{
public:
    frontier_req ();
    bool deserialize (logos::stream &) override;
    void serialize (logos::stream &) override;
    void visit (logos::message_visitor &) const override;
    bool operator== (logos::frontier_req const &) const;
    logos::account start;
    uint32_t age;
    uint32_t count;
    uint64_t nr_delegate; // total number of delegates we are requesting frontier for.
};
class bulk_pull : public message
{
public:
    bulk_pull ();
    bool deserialize (logos::stream &) override; // Need to implement these for us.
    void serialize (logos::stream &) override;
    void visit (logos::message_visitor &) const override;
    logos::uint256_union start;
    logos::block_hash end;
    uint64_t timestamp_start;
    uint64_t timestamp_end;
    uint64_t seq_start;
    uint64_t seq_end;
    int      delegate_id; // Call for each delegate.
    BlockHash e_start;
    BlockHash e_end;
    BlockHash m_start; // I think this has to point to micro/epoch blocks.
    BlockHash m_end;
    BlockHash b_start;
    BlockHash b_end;

    static constexpr int SIZE = 
                        sizeof (logos::uint256_union)   // start
                        + sizeof (logos::uint256_union) // end
                        + sizeof(uint64_t) // timestamp_start
                        + sizeof(uint64_t) // timestamp_end
                        + sizeof(uint64_t) // seq_start
                        + sizeof(uint64_t) // seq_end
                        + sizeof(int)      // delegate_id
                        + sizeof(logos::uint256_union) + sizeof(logos::uint256_union)  // e_start-e_end
                        + sizeof(logos::uint256_union) + sizeof(logos::uint256_union)  // m_start-m_end
                        + sizeof(logos::uint256_union) + sizeof(logos::uint256_union); // b_start-b_end
    friend
    std::ostream& operator<<(std::ostream &out, const bulk_pull &obj)
    {
        out << " bulk_pull: ts start: " << obj.timestamp_start << " ts end: " << obj.timestamp_end << " seq_end: " << obj.seq_end
            << " delegate_id: " << obj.delegate_id << " e_start: " << obj.e_start.to_string() << " e_end: " << obj.e_end.to_string()
            << " m_start: " << obj.m_start.to_string() << " m_end: " << obj.m_end.to_string() 
            << " b_start: " << obj.b_start.to_string() << " b_end: " << obj.b_end.to_string() << "\n";
        return out;
    }

};
class bulk_pull_blocks : public message
{
public:
    bulk_pull_blocks ();
    bool deserialize (logos::stream &) override;
    void serialize (logos::stream &) override;
    void visit (logos::message_visitor &) const override;
    logos::block_hash min_hash;
    logos::block_hash max_hash;
    bulk_pull_blocks_mode mode;
    uint32_t max_count;
};
class bulk_push : public message
{
public:
    bulk_push ();
    bool deserialize (logos::stream &) override;
    void serialize (logos::stream &) override;
    void visit (logos::message_visitor &) const override;
};
class message_visitor
{
public:
    virtual void keepalive (logos::keepalive const &) = 0;
    virtual void bulk_pull (logos::bulk_pull const &) = 0;
    virtual void bulk_pull_blocks (logos::bulk_pull_blocks const &) = 0;
    virtual void bulk_push (logos::bulk_push const &) = 0;
    virtual void frontier_req (logos::frontier_req const &) = 0;
    virtual ~message_visitor ();
};

/**
 * Returns seconds passed since unix epoch (posix time)
 */
inline uint64_t seconds_since_epoch ()
{
    return std::chrono::duration_cast<std::chrono::seconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();
}
}
