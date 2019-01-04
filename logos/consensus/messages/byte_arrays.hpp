#ifndef LOGOS_CONSENSUS_MESSAGES_BYTE_ARRAYS_HPP_
#define LOGOS_CONSENSUS_MESSAGES_BYTE_ARRAYS_HPP_

#include <endian.h>
#include <logos/lib/blocks.hpp>

static const size_t HASH_SIZE                   = 32;
static const size_t ACCOUNT_ADDRESS_SIZE        = 32;
static const size_t ACCOUNT_PUB_KEY_SIZE        = 32;
static const size_t ACCOUNT_PRIV_KEY_SIZE       = 32;
static const size_t ACCOUNT_SIG_SIZE            = 64;
static const size_t ACCOUNT_AMOUNT_SIZE         = 16;

static const size_t CONSENSUS_SIG_SIZE          = 32;
static const size_t CONSENSUS_PUB_KEY_SIZE      = 64;
static const size_t CONSENSUS_PRIV_KEY_SIZE     = 32;

using byte                  = unsigned char;

template<size_t len>
struct ByteArray : public std::array<byte, len>
{
    ByteArray()
    {
        memset(this->data(), 0, len);
    }

    ByteArray(void * buf, size_t buf_len)
    {
        assert(buf_len == len);
        memcpy(this->data(), buf, len);
    }

    void clear()
    {
        memset(this->data(), 0, len);
    }

    ByteArray(uint8_t v)
    {
        memset(this->data(), v, len);
    }

    // return error
    bool SetValue(const void * buf, size_t buf_len)
    {
        assert(buf_len == len);
        if(buf_len == len)
        {
            memcpy(this->data(), buf, buf_len);
            return false;
        }
        return true;
    }

    std::string to_string () const
    {
        std::stringstream stream;
        auto d = this->data();
        for(size_t i = 0; i < len; ++i)
        {
            stream << std::hex << std::noshowbase << std::setw (2) << std::setfill ('0') << (int)(d[i]);
        }
        return stream.str ();
    }

    void Hash(blake2b_state & hash) const
    {
        blake2b_update(&hash, this->data(), len);
    }

    bool is_zero () const
    {
        auto d = this->data();
        for(size_t i = 0; i < len; ++i)
        {
            if(d[i] != 0)
                return false;
        }
        return true;
    }

    bool operator!= (ByteArray<len> const & other_a) const
    {
        return !(*this == other_a);
    }

    bool operator== (ByteArray<len> const & other_a) const
    {
        return memcmp(this->data(), other_a.data(), len) == 0;
    }

};


namespace logos
{
template <size_t len>
bool read (logos::stream & stream_a, ByteArray<len> & value)
{
    auto amount_read (stream_a.sgetn (value.data(), len));
    return amount_read != len;
}
template <size_t len>
uint32_t write (logos::stream & stream_a, const ByteArray<len> & value)
{
    auto amount_written (stream_a.sputn (value.data(), len));
    assert (amount_written == len);
    return amount_written;
}
}

namespace std
{
template <size_t len>
struct hash<ByteArray<len>>
{
    size_t operator() (ByteArray<len> const & data_a) const
    {
        return *reinterpret_cast<size_t const *> (data_a.data ());
    }
};
}

#if 0
using BlockHash             = ByteArray<HASH_SIZE>;
using DelegateSig           = ByteArray<CONSENSUS_SIG_SIZE>;
using DelegatePubKey        = ByteArray<CONSENSUS_PUB_KEY_SIZE>;
using DelegatePrivKey       = ByteArray<CONSENSUS_PRIV_KEY_SIZE>;

using AccountAddress        = ByteArray<ACCOUNT_ADDRESS_SIZE>;
using AccountPubKey         = ByteArray<ACCOUNT_PUB_KEY_SIZE>;
using AccountPrivKey        = ByteArray<ACCOUNT_PRIV_KEY_SIZE>;
using AccountSig            = ByteArray<ACCOUNT_SIG_SIZE>;

using Amount                = logos::amount;//TODO
#else
using BlockHash             = logos::uint256_union;
using DelegateSig           = ByteArray<CONSENSUS_SIG_SIZE>;//logos::uint256_union;
using DelegatePubKey        = ByteArray<CONSENSUS_PUB_KEY_SIZE>;//logos::uint512_union;
using DelegatePrivKey       = ByteArray<CONSENSUS_PRIV_KEY_SIZE>;//logos::uint256_union;

using AccountAddress        = logos::uint256_union;
using AccountPubKey         = logos::uint256_union;
using AccountPrivKey        = logos::uint256_union;
using AccountSig            = logos::uint512_union;

using Amount                = logos::amount;
#endif


#endif /* LOGOS_CONSENSUS_MESSAGES_BYTE_ARRAYS_HPP_ */
