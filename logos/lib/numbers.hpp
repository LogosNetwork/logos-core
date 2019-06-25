#pragma once

#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <cryptopp/osrng.h>
#include <blake2/blake2.h>

using Float50  = boost::multiprecision::cpp_dec_float_50;
using Float100 = boost::multiprecision::cpp_dec_float_100;

namespace logos
{
// Random pool used by Logos.
// This must be thread_local as long as the AutoSeededRandomPool implementation requires it
extern thread_local CryptoPP::AutoSeededRandomPool random_pool;
using uint128_t = boost::multiprecision::uint128_t;
using uint256_t = boost::multiprecision::uint256_t;
using uint512_t = boost::multiprecision::uint512_t;
// SI dividers
logos::uint128_t const Glgs_ratio = logos::uint128_t ("1000000000000000000000000000000000"); // 10^33
logos::uint128_t const Mlgs_ratio = logos::uint128_t ("1000000000000000000000000000000"); // 10^30
logos::uint128_t const klgs_ratio = logos::uint128_t ("1000000000000000000000000000"); // 10^27
logos::uint128_t const lgs_ratio = logos::uint128_t ("1000000000000000000000000"); // 10^24
logos::uint128_t const mlgs_ratio = logos::uint128_t ("1000000000000000000000"); // 10^21
logos::uint128_t const ulgs_ratio = logos::uint128_t ("1000000000000000000"); // 10^18

// We operate on streams of uint8_t by convention
using stream = std::basic_streambuf<uint8_t>;

class mdb_val;

union uint128_union
{
public:
    static_assert (sizeof(uint128_t)==16, "sizeof(uint128_t) != 16");

    uint128_union ()
    {
        memset(bytes.data(), 0, 16);
    }
    uint128_union (std::string const &);
    uint128_union (uint64_t);
    uint128_union (logos::uint128_union const &) = default;
    uint128_union (logos::uint128_t const &);
    bool Deserialize(logos::stream & stream);
    uint32_t Serialize(logos::stream & stream) const;
    uint128_union operator+ (logos::uint128_union const &) const;
    uint128_union operator- (logos::uint128_union const &) const;
    uint128_union operator* (logos::uint128_union const &) const;
    bool operator== (logos::uint128_union const &) const;
    bool operator!= (logos::uint128_union const &) const;
    bool operator< (logos::uint128_union const &) const;
    bool operator> (logos::uint128_union const &) const;
    bool operator<= (logos::uint128_union const &) const;
    bool operator>= (logos::uint128_union const &) const;
    uint128_union & operator+=(const uint128_union & other);
    uint128_union & operator-=(const uint128_union & other);
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
    logos::mdb_val to_mdb_val(std::vector<uint8_t>& buf) const;
    std::array<uint8_t, 16> bytes;
    std::array<char, 16> chars;
    std::array<uint32_t, 4> dwords;
    std::array<uint64_t, 2> qwords;

    const uint8_t * data() const
    {
        return bytes.data();
    }
    void Hash(blake2b_state & hash) const
    {
        blake2b_update(&hash, bytes.data(), 16);
    }

    operator std::array<uint8_t, 16>() { return bytes; }
};
// Balances are 128 bit.
using amount = uint128_union;
class raw_key;
union uint256_union
{
    uint256_union ()
    {
        memset(bytes.data(), 0, 32);
    }
    uint256_union (std::string const &);
    uint256_union (uint64_t);
    uint256_union (logos::uint256_t const &);
    uint256_union (std::array<uint8_t, 32> const &);
    void encrypt (logos::raw_key const &, logos::raw_key const &, uint128_union const &);
    uint256_union & operator^= (logos::uint256_union const &);
    uint256_union operator^ (logos::uint256_union const &) const;
    bool operator== (logos::uint256_union const &) const;
    bool operator!= (logos::uint256_union const &) const;
    bool operator< (logos::uint256_union const &) const;
    uint256_union operator+ (logos::uint256_union const &) const;
    uint256_union operator- (logos::uint256_union const &) const;
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

    uint8_t * data()
    {
        return bytes.data();
    }

    const uint8_t * data() const
    {
        return bytes.data();
    }

    void Hash(blake2b_state & hash) const
    {
        blake2b_update(&hash, bytes.data(), 32);
    }
    uint256_union(void * buf, size_t buf_len)
    {
        assert(buf_len == 32);
        memcpy(data(), buf, 32);
    }

    operator std::array<uint8_t, 32>() { return bytes; }
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
    uint512_union (uint8_t t = 0)
    {
        memset(bytes.data(), t, 64);
    }

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

    uint8_t * data()
    {
        return bytes.data();
    }

    const uint8_t * data() const
    {
        return bytes.data();
    }

    void Hash(blake2b_state & hash) const
    {
        blake2b_update(&hash, bytes.data(), 64);
    }

    operator std::array<uint8_t, 64>() { return bytes; }
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

namespace logos
{

inline
size_t hash_value(const logos::block_hash & hash)
{
    return std::hash<logos::block_hash>()(hash);
}

}
