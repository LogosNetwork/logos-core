// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RANDOM_H
#define BITCOIN_RANDOM_H

#include <stdint.h>
#include <limits>
#include <atomic>
#include <crypto/chacha20.h>
#include <crypto/common.h>
#include <uint256.h>
#include <logging.h>
#include <sync.h>

class Random
{
public:
    Random(BCLog::Logger &logger);
    ~Random();

    /** Check that OS randomness is available and returning the requested number
     * of bytes.
     */
    bool SanityCheck();

    /* Seed OpenSSL PRNG with additional entropy data */
    void RandAddSeed();

    /**
     * Functions to gather random data via the OpenSSL PRNG
     */
    void GetRandBytes(unsigned char* buf, int num);
    uint64_t GetRand(uint64_t nMax);
    int GetRandInt(int nMax);
    uint256 GetRandHash();

private:
    BCLog::Logger &                             logger_;
    static std::atomic<int>                     ninstances;
    static std::unique_ptr<CCriticalSection[]>  ppmutexOpenSSL;

    static void locking_callback(int mode, int i, const char* file, int line) NO_THREAD_SAFETY_ANALYSIS;

    [[noreturn]] void RandFailure();
    void GetDevURandom(unsigned char *ent32);
    void GetOSRand(unsigned char *ent32);
};

/**
 * Fast randomness source. This is seeded once with secure random data, but
 * is completely deterministic and insecure after that.
 * This class is not thread-safe.
 */
class FastRandomContext
{
private:
    Random &        random_;
    bool            requires_seed;
    ChaCha20        rng;

    unsigned char   bytebuf[64];
    int             bytebuf_size;

    uint64_t        bitbuf;
    int             bitbuf_size;

    void RandomSeed();

    void FillByteBuffer()
    {
        if (requires_seed)
            RandomSeed();
        rng.Output(bytebuf, sizeof(bytebuf));
        bytebuf_size = sizeof(bytebuf);
    }

    void FillBitBuffer()
    {
        bitbuf = rand64();
        bitbuf_size = 64;
    }

public:
    explicit FastRandomContext(Random &random,
                               bool fDeterministic = false);

    /** Initialize with explicit seed (only for testing) */
    explicit FastRandomContext(Random &random, const uint256& seed);

    /** Generate a random 64-bit integer. */
    uint64_t rand64()
    {
        if (bytebuf_size < 8)
            FillByteBuffer();
        uint64_t ret = ReadLE64(bytebuf + 64 - bytebuf_size);
        bytebuf_size -= 8;
        return ret;
    }

    /** Generate a random (bits)-bit integer. */
    uint64_t randbits(int bits)
    {
        if (bits == 0)
            return 0;
        else if (bits > 32)
            return rand64() >> (64 - bits);
        else
        {
            if (bitbuf_size < bits)
                FillBitBuffer();
            uint64_t ret = bitbuf & (~(uint64_t)0 >> (64 - bits));
            bitbuf >>= bits;
            bitbuf_size -= bits;
            return ret;
        }
    }

    /** Generate a random integer in the range [0..range). */
    uint64_t randrange(uint64_t range)
    {
        --range;
        int bits = CountBits(range);
        while (true)
        {
            uint64_t ret = randbits(bits);
            if (ret <= range) return ret;
        }
    }

    /** Generate random bytes. */
    std::vector<unsigned char> randbytes(size_t len);

    /** Generate a random 32-bit integer. */
    uint32_t rand32()
    {
        return randbits(32);
    }

    /** generate a random uint256. */
    uint256 rand256();

    /** Generate a random boolean. */
    bool randbool() {
        return randbits(1);
    }

    // Compatibility with the C++11 UniformRandomBitGenerator concept
    typedef uint64_t result_type;
    static constexpr uint64_t min()
    {
        return 0;
    }
    static constexpr uint64_t max()
    {
        return std::numeric_limits<uint64_t>::max();
    }
    inline uint64_t operator()()
    {
        return rand64();
    }
};

#endif // BITCOIN_RANDOM_H
