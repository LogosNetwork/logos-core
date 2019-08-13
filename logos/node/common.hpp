#pragma once

#include <logos/common.hpp>
#include <logos/lib/interface.h>

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

/**
 * Returns seconds passed since unix epoch (posix time)
 */
inline uint64_t seconds_since_epoch ()
{
    return std::chrono::duration_cast<std::chrono::seconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();
}
}
