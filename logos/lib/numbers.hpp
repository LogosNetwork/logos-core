#pragma once

#include <boost/multiprecision/cpp_int.hpp>

#include <cryptopp/osrng.h>

namespace logos
{
// Random pool used by Logos.
// This must be thread_local as long as the AutoSeededRandomPool implementation requires it
extern thread_local CryptoPP::AutoSeededRandomPool random_pool;
using uint128_t = boost::multiprecision::uint128_t;
using uint256_t = boost::multiprecision::uint256_t;
using uint512_t = boost::multiprecision::uint512_t;
// SI dividers
logos::uint128_t const Gxrb_ratio = logos::uint128_t ("1000000000000000000000000000000000"); // 10^33
logos::uint128_t const Mxrb_ratio = logos::uint128_t ("1000000000000000000000000000000"); // 10^30
logos::uint128_t const kxrb_ratio = logos::uint128_t ("1000000000000000000000000000"); // 10^27
logos::uint128_t const xrb_ratio = logos::uint128_t ("1000000000000000000000000"); // 10^24
logos::uint128_t const mxrb_ratio = logos::uint128_t ("1000000000000000000000"); // 10^21
logos::uint128_t const uxrb_ratio = logos::uint128_t ("1000000000000000000"); // 10^18

union uint128_union
{
public:
    uint128_union () = default;
    uint128_union (std::string const &);
    uint128_union (uint64_t);
    uint128_union (logos::uint128_union const &) = default;
    uint128_union (logos::uint128_t const &);
    bool operator== (logos::uint128_union const &) const;
    bool operator!= (logos::uint128_union const &) const;
    bool operator< (logos::uint128_union const &) const;
    bool operator> (logos::uint128_union const &) const;
    void encode_hex (std::string &) const;
    bool decode_hex (std::string const &);
    void encode_dec (std::string &) const;
    bool decode_dec (std::string const &);
    std::string format_balance (logos::uint128_t scale, int precision, bool group_digits);
    std::string format_balance (logos::uint128_t scale, int precision, bool group_digits, const std::locale & locale);
    logos::uint128_t number () const;
    void clear ();
    bool is_zero () const;
    std::string to_string () const;
    std::string to_string_dec () const;
    std::array<uint8_t, 16> bytes;
    std::array<char, 16> chars;
    std::array<uint32_t, 4> dwords;
    std::array<uint64_t, 2> qwords;
};
// Balances are 128 bit.
using amount = uint128_union;
class raw_key;
union uint256_union
{
    uint256_union () = default;
    uint256_union (std::string const &);
    uint256_union (uint64_t);
    uint256_union (logos::uint256_t const &);
    void encrypt (logos::raw_key const &, logos::raw_key const &, uint128_union const &);
    uint256_union & operator^= (logos::uint256_union const &);
    uint256_union operator^ (logos::uint256_union const &) const;
    bool operator== (logos::uint256_union const &) const;
    bool operator!= (logos::uint256_union const &) const;
    bool operator< (logos::uint256_union const &) const;
    void encode_hex (std::string &) const;
    bool decode_hex (std::string const &);
    void encode_dec (std::string &) const;
    bool decode_dec (std::string const &);
    void encode_account (std::string &) const;
    std::string to_account () const;
    std::string to_account_split () const;
    bool decode_account (std::string const &);
    std::array<uint8_t, 32> bytes;
    std::array<char, 32> chars;
    std::array<uint32_t, 8> dwords;
    std::array<uint64_t, 4> qwords;
    std::array<uint128_union, 2> owords;
    void clear ();
    bool is_zero () const;
    std::string to_string () const;
    logos::uint256_t number () const;
};
// All keys and hashes are 256 bit.
using block_hash = uint256_union;
using account = uint256_union;
using public_key = uint256_union;
using private_key = uint256_union;
using secret_key = uint256_union;
using checksum = uint256_union;
class raw_key
{
public:
    raw_key () = default;
    ~raw_key ();
    void decrypt (logos::uint256_union const &, logos::raw_key const &, uint128_union const &);
    raw_key (logos::raw_key const &) = delete;
    raw_key (logos::raw_key const &&) = delete;
    logos::raw_key & operator= (logos::raw_key const &) = delete;
    bool operator== (logos::raw_key const &) const;
    bool operator!= (logos::raw_key const &) const;
    logos::uint256_union data;
};
union uint512_union
{
    uint512_union () = default;
    uint512_union (logos::uint512_t const &);
    bool operator== (logos::uint512_union const &) const;
    bool operator!= (logos::uint512_union const &) const;
    logos::uint512_union & operator^= (logos::uint512_union const &);
    void encode_hex (std::string &) const;
    bool decode_hex (std::string const &);
    std::array<uint8_t, 64> bytes;
    std::array<uint32_t, 16> dwords;
    std::array<uint64_t, 8> qwords;
    std::array<uint256_union, 2> uint256s;
    void clear ();
    logos::uint512_t number () const;
    std::string to_string () const;
};
// Only signatures are 512 bit.
using signature = uint512_union;

logos::uint512_union sign_message (logos::raw_key const &, logos::public_key const &, logos::uint256_union const &);
bool validate_message (logos::public_key const &, logos::uint256_union const &, logos::uint512_union const &);
void deterministic_key (logos::uint256_union const &, uint32_t, logos::uint256_union &);
}

namespace std
{
template <>
struct hash<logos::uint256_union>
{
    size_t operator() (logos::uint256_union const & data_a) const
    {
        return *reinterpret_cast<size_t const *> (data_a.bytes.data ());
    }
};
template <>
struct hash<logos::uint256_t>
{
    size_t operator() (logos::uint256_t const & number_a) const
    {
        return number_a.convert_to<size_t> ();
    }
};
}
