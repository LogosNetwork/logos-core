#pragma once

#include <logos/common.hpp>
#include <logos/lib/interface.h>

#include <boost/asio.hpp>

#include <bitset>

#include <xxhash/xxhash.h>

namespace logos
{

using endpoint = boost::asio::ip::udp::endpoint;
using tcp_endpoint = boost::asio::ip::tcp::endpoint;

bool parse_port (std::string const &, uint16_t &);

/**
 * Returns seconds passed since unix epoch (posix time)
 */
inline uint64_t seconds_since_epoch ()
{
    return std::chrono::duration_cast<std::chrono::seconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();
}

}
