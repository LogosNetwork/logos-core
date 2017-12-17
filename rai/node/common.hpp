#pragma once

#include <rai/interface.h>
#include <rai/secure.hpp>

#include <boost/asio.hpp>

#include <bitset>

#include <xxhash/xxhash.h>

namespace rai
{
using endpoint = boost::asio::ip::udp::endpoint;
bool parse_port (std::string const &, uint16_t &);
bool parse_address_port (std::string const &, boost::asio::ip::address &, uint16_t &);
using tcp_endpoint = boost::asio::ip::tcp::endpoint;
bool parse_endpoint (std::string const &, rai::endpoint &);
bool parse_tcp_endpoint (std::string const &, rai::tcp_endpoint &);
bool reserved_address (rai::endpoint const &);
}
static uint64_t endpoint_hash_raw (rai::endpoint const & endpoint_a)
{
	assert (endpoint_a.address ().is_v6 ());
	rai::uint128_union address;
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
struct endpoint_hash <8>
{
    size_t operator () (rai::endpoint const & endpoint_a) const
    {
		return endpoint_hash_raw (endpoint_a);
    }
};
template <>
struct endpoint_hash <4>
{
    size_t operator () (rai::endpoint const & endpoint_a) const
    {
		uint64_t big (endpoint_hash_raw (endpoint_a));
		uint32_t result (static_cast <uint32_t> (big) ^ static_cast <uint32_t> (big >> 32));
		return result;
    }
};
template <>
struct hash <rai::endpoint>
{
    size_t operator () (rai::endpoint const & endpoint_a) const
    {
        endpoint_hash <sizeof (size_t)> ehash;
        return ehash (endpoint_a);
    }
};
}
namespace boost
{
template <>
struct hash <rai::endpoint>
{
    size_t operator () (rai::endpoint const & endpoint_a) const
    {
        std::hash <rai::endpoint> hash;
        return hash (endpoint_a);
    }
};
}

namespace rai
{
enum class message_type : uint8_t
{
    invalid,
    not_a_type,
    keepalive,
    publish,
    confirm_req,
    confirm_ack,
    bulk_pull,
    bulk_push,
    frontier_req
};
class message_visitor;
class message
{
public:
    message (rai::message_type);
	message (bool &, rai::stream &);
    virtual ~message () = default;
    void write_header (rai::stream &);
    static bool read_header (rai::stream &, uint8_t &, uint8_t &, uint8_t &, rai::message_type &, std::bitset <16> &);
    virtual void serialize (rai::stream &) = 0;
    virtual bool deserialize (rai::stream &) = 0;
    virtual void visit (rai::message_visitor &) const = 0;
    rai::block_type block_type () const;
    void block_type_set (rai::block_type);
    bool ipv4_only ();
    void ipv4_only_set (bool);
	static std::array <uint8_t, 2> constexpr magic_number = rai::rai_network == rai::rai_networks::rai_test_network ? std::array <uint8_t, 2>({ 'R', 'A' }) : rai::rai_network == rai::rai_networks::rai_beta_network ? std::array <uint8_t, 2>({ 'R', 'B' }) : std::array <uint8_t, 2>({ 'R', 'C' });
    uint8_t version_max;
    uint8_t version_using;
    uint8_t version_min;
    rai::message_type type;
    std::bitset <16> extensions;
    static size_t constexpr ipv4_only_position = 1;
    static size_t constexpr bootstrap_server_position = 2;
    static std::bitset <16> constexpr block_type_mask = std::bitset <16> (0x0f00);
};
class work_pool;
class message_parser
{
public:
    message_parser (rai::message_visitor &, rai::work_pool &);
    void deserialize_buffer (uint8_t const *, size_t);
    void deserialize_keepalive (uint8_t const *, size_t);
    void deserialize_publish (uint8_t const *, size_t);
    void deserialize_confirm_req (uint8_t const *, size_t);
    void deserialize_confirm_ack (uint8_t const *, size_t);
    bool at_end (rai::bufferstream &);
    rai::message_visitor & visitor;
	rai::work_pool & pool;
    bool error;
    bool insufficient_work;
};
class keepalive : public message
{
public:
    keepalive ();
    void visit (rai::message_visitor &) const override;
    bool deserialize (rai::stream &) override;
    void serialize (rai::stream &) override;
    bool operator == (rai::keepalive const &) const;
    std::array <rai::endpoint, 8> peers;
};
class publish : public message
{
public:
    publish ();
    publish (std::shared_ptr <rai::block>);
    void visit (rai::message_visitor &) const override;
    bool deserialize (rai::stream &) override;
    void serialize (rai::stream &) override;
    bool operator == (rai::publish const &) const;
    std::shared_ptr <rai::block> block;
};
class confirm_req : public message
{
public:
    confirm_req ();
    confirm_req (std::shared_ptr <rai::block>);
    bool deserialize (rai::stream &) override;
    void serialize (rai::stream &) override;
    void visit (rai::message_visitor &) const override;
    bool operator == (rai::confirm_req const &) const;
    std::shared_ptr <rai::block> block;
};
class confirm_ack : public message
{
public:
	confirm_ack (bool &, rai::stream &);
	confirm_ack (std::shared_ptr <rai::vote>);
    bool deserialize (rai::stream &) override;
    void serialize (rai::stream &) override;
    void visit (rai::message_visitor &) const override;
    bool operator == (rai::confirm_ack const &) const;
	std::shared_ptr <rai::vote> vote;
};
class frontier_req : public message
{
public:
    frontier_req ();
    bool deserialize (rai::stream &) override;
    void serialize (rai::stream &) override;
    void visit (rai::message_visitor &) const override;
    bool operator == (rai::frontier_req const &) const;
    rai::account start;
    uint32_t age;
    uint32_t count;
};
class bulk_pull : public message
{
public:
    bulk_pull ();
    bool deserialize (rai::stream &) override;
    void serialize (rai::stream &) override;
    void visit (rai::message_visitor &) const override;
    rai::uint256_union start;
    rai::block_hash end;
    uint32_t count;
};
class bulk_push : public message
{
public:
    bulk_push ();
    bool deserialize (rai::stream &) override;
    void serialize (rai::stream &) override;
    void visit (rai::message_visitor &) const override;
};
class message_visitor
{
public:
    virtual void keepalive (rai::keepalive const &) = 0;
    virtual void publish (rai::publish const &) = 0;
    virtual void confirm_req (rai::confirm_req const &) = 0;
    virtual void confirm_ack (rai::confirm_ack const &) = 0;
    virtual void bulk_pull (rai::bulk_pull const &) = 0;
    virtual void bulk_push (rai::bulk_push const &) = 0;
    virtual void frontier_req (rai::frontier_req const &) = 0;
};
template <typename ... T>
class observer_set
{
public:
	void add (std::function <void (T...)> const & observer_a)
	{
		std::lock_guard <std::mutex> lock (mutex);
		observers.push_back (observer_a);
	}
	void operator () (T ... args)
	{
		std::lock_guard <std::mutex> lock (mutex);
		for (auto & i: observers)
		{
			i (args...);
		}
	}
	std::mutex mutex;
	std::vector <std::function <void (T...)>> observers;
};
}
