// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UINT256_H
#define BITCOIN_UINT256_H

#include <assert.h>
#include <stdint.h>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <crypto/common.h>

/** Template base class for fixed-sized opaque blobs. */
template<unsigned int BITS>
class base_blob
{
protected:
    static constexpr int    WIDTH = BITS / 8;
    uint8_t                 data[WIDTH];
public:
    base_blob()
    {
        memset(data, 0, sizeof(data));
    }

    explicit base_blob(const std::vector<unsigned char>& vch);

    bool IsNull() const
    {
        for (int i = 0; i < WIDTH; i++)
            if (data[i] != 0)
                return false;
        return true;
    }

    void SetNull()
    {
        memset(data, 0, sizeof(data));
    }

    inline int Compare(const base_blob& other) const
    {
        return memcmp(data, other.data, sizeof(data));
    }

    friend inline bool operator==(const base_blob& a, const base_blob& b)
    {
        return a.Compare(b) == 0;
    }
    friend inline bool operator!=(const base_blob& a, const base_blob& b)
    {
        return a.Compare(b) != 0;
    }
    friend inline bool operator<(const base_blob& a, const base_blob& b)
    {
        return a.Compare(b) < 0;
    }

    std::string GetHex() const;
    std::string ToString() const;

    unsigned char* begin()
    {
        return &data[0];
    }

    unsigned char* end()
    {
        return &data[WIDTH];
    }

    const unsigned char* begin() const
    {
        return &data[0];
    }

    const unsigned char* end() const
    {
        return &data[WIDTH];
    }

    unsigned int size() const
    {
        return sizeof(data);
    }

    uint64_t GetUint64(int pos) const
    {
        const uint8_t* ptr = data + pos * 8;
        return ((uint64_t)ptr[0])
                | ((uint64_t)ptr[1]) << 8
                | ((uint64_t)ptr[2]) << 16
                | ((uint64_t)ptr[3]) << 24
                | ((uint64_t)ptr[4]) << 32
                | ((uint64_t)ptr[5]) << 40
                | ((uint64_t)ptr[6]) << 48
                | ((uint64_t)ptr[7]) << 56;
    }

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        s.write((char*)data, sizeof(data));
    }

    template<typename Stream>
    void Unserialize(Stream& s)
    {
        s.read((char*)data, sizeof(data));
    }
};

/** 256-bit opaque blob.
 * @note This type is called uint256 for historical reasons only. It is an
 * opaque blob of 256 bits and has no integer operations. Use arith_uint256 if
 * those are required.
 */
class uint256 : public base_blob<256>
{
public:
    uint256()
    {
    }
    explicit uint256(const std::vector<unsigned char>& vch)
        : base_blob<256>(vch)
    {
    }

    /** A cheap hash function that just returns 64 bits from the result, it can be
     * used when the contents are considered uniformly random. It is not appropriate
     * when the value can easily be influenced from outside as e.g. a network adversary could
     * provide values to trigger worst-case behavior.
     */
    uint64_t GetCheapHash(size_t offset = 0) const
    {
        return ReadLE64(data + offset);
    }
};

#endif // BITCOIN_UINT256_H
