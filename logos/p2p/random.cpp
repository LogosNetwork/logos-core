// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stdlib.h>
#include <fcntl.h>
#include <sys/time.h>
#include <chrono>
#include <thread>
#include <mutex>
#include <thread>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <openssl/conf.h>
#include <openssl/err.h>
#include <boost/thread.hpp>

#if defined(__x86_64__) || defined(__amd64__) || defined(__i386__)
#include <cpuid.h>
#endif

#include <random.h>
#include <hash.h>
#include <support/cleanse.h>
#include <logging.h>  // for LogPrint()
#include <sync.h>     // for WAIT_LOCK

/* Number of random bytes returned by GetOSRand.
 * When changing this constant make sure to change all call sites, and make
 * sure that the underlying OS APIs for all platforms support the number.
 * (many cap out at 256 bytes).
 */
constexpr int NUM_OS_RANDOM_BYTES = 32;

std::atomic<int> Random::ninstances(0);

/** Init OpenSSL library multithreading support */
std::unique_ptr<CCriticalSection[]> Random::ppmutexOpenSSL;

void Random::locking_callback(int mode, int i, const char* file, int line) NO_THREAD_SAFETY_ANALYSIS
{
    if (mode & CRYPTO_LOCK)
    {
        ENTER_CRITICAL_SECTION(ppmutexOpenSSL[i]);
    }
    else
    {
        LEAVE_CRITICAL_SECTION(ppmutexOpenSSL[i]);
    }
}

Random::Random(BCLog::Logger &logger)
    : logger_(logger)
{
    if (ninstances++) return;

    // Init OpenSSL library multithreading support
    ppmutexOpenSSL.reset(new CCriticalSection[CRYPTO_num_locks()]);
    CRYPTO_set_locking_callback(locking_callback);

    // OpenSSL can optionally load a config file which lists optional loadable modules and engines.
    // We don't use them so we don't require the config. However some of our libs may call functions
    // which attempt to load the config file, possibly resulting in an exit() or crash if it is missing
    // or corrupt. Explicitly tell OpenSSL not to try to load the file. The result for our libs will be
    // that the config appears to have been loaded and there are no modules/engines available.
    OPENSSL_no_config();

    // Seed OpenSSL PRNG with performance counter
    RandAddSeed();
}

Random::~Random()
{
    if (--ninstances) return;

    // Securely erase the memory used by the PRNG
    RAND_cleanup();
    // Shutdown OpenSSL library multithreading support
    CRYPTO_set_locking_callback(nullptr);
    // Clear the set of locks now to maintain symmetry with the constructor.
    ppmutexOpenSSL.reset();
}

[[noreturn]] void Random::RandFailure()
{
    LogPrintf("Failed to read randomness, aborting\n");
    std::abort();
}

static inline int64_t GetPerformanceCounter()
{
    // Read the hardware time stamp counter when available.
    // See https://en.wikipedia.org/wiki/Time_Stamp_Counter for more information.
#if defined(__i386__)
    uint64_t r = 0;
    __asm__ volatile ("rdtsc" : "=A"(r)); // Constrain the r variable to the eax:edx pair.
    return r;
#elif defined(__x86_64__) || defined(__amd64__)
    uint64_t r1 = 0, r2 = 0;
    __asm__ volatile ("rdtsc" : "=a"(r1), "=d"(r2)); // Constrain r1 to rax and r2 to rdx.
    return (r2 << 32) | r1;
#else
    // Fall back to using C++11 clock (usually microsecond or nanosecond precision)
    return std::chrono::high_resolution_clock::now().time_since_epoch().count();
#endif
}

void Random::RandAddSeed()
{
    // Seed with CPU performance counter
    int64_t nCounter = GetPerformanceCounter();
    RAND_add(&nCounter, sizeof(nCounter), 1.5);
    memory_cleanse((void*)&nCounter, sizeof(nCounter));
}

/** Fallback: get 32 bytes of system entropy from /dev/urandom. The most
 * compatible way to get cryptographic randomness on UNIX-ish platforms.
 */
void Random::GetDevURandom(unsigned char *ent32)
{
    int f = open("/dev/urandom", O_RDONLY);
    if (f == -1)
        RandFailure();
    int have = 0;
    do
    {
        ssize_t n = read(f, ent32 + have, NUM_OS_RANDOM_BYTES - have);
        if (n <= 0 || n + have > NUM_OS_RANDOM_BYTES)
        {
            close(f);
            RandFailure();
        }
        have += n;
    }
    while (have < NUM_OS_RANDOM_BYTES);
    close(f);
}

/** Get 32 bytes of system entropy. */
void Random::GetOSRand(unsigned char *ent32)
{
    /* Fall back to /dev/urandom if there is no specific method implemented to
     * get system entropy for this OS.
     */
    GetDevURandom(ent32);
}

void Random::GetRandBytes(unsigned char* buf, int num)
{
    if (RAND_bytes(buf, num) != 1)
        RandFailure();
}

uint64_t Random::GetRand(uint64_t nMax)
{
    if (nMax == 0)
        return 0;

    // The range of the random source must be a multiple of the modulus
    // to give every possible output value an equal possibility
    uint64_t nRange = (std::numeric_limits<uint64_t>::max() / nMax) * nMax;
    uint64_t nRand = 0;
    do
    {
        GetRandBytes((unsigned char*)&nRand, sizeof(nRand));
    }
    while (nRand >= nRange);
    return (nRand % nMax);
}

int Random::GetRandInt(int nMax)
{
    return GetRand(nMax);
}

uint256 Random::GetRandHash()
{
    uint256 hash;
    GetRandBytes((unsigned char*)&hash, sizeof(hash));
    return hash;
}

void FastRandomContext::RandomSeed()
{
    uint256 seed = random_.GetRandHash();
    rng.SetKey(seed.begin(), 32);
    requires_seed = false;
}

uint256 FastRandomContext::rand256()
{
    if (bytebuf_size < 32)
        FillByteBuffer();
    uint256 ret;
    memcpy(ret.begin(), bytebuf + 64 - bytebuf_size, 32);
    bytebuf_size -= 32;
    return ret;
}

std::vector<unsigned char> FastRandomContext::randbytes(size_t len)
{
    std::vector<unsigned char> ret(len);
    if (len > 0)
        rng.Output(&ret[0], len);
    return ret;
}

FastRandomContext::FastRandomContext(Random &random,
                                     const uint256& seed)
    : random_(random)
    , requires_seed(false)
    , bytebuf_size(0)
    , bitbuf_size(0)
{
    rng.SetKey(seed.begin(), 32);
}

bool Random::SanityCheck()
{
    uint64_t start = GetPerformanceCounter();

    /* This does not measure the quality of randomness, but it does test that
     * OSRandom() overwrites all 32 bytes of the output given a maximum
     * number of tries.
     */
    constexpr ssize_t MAX_TRIES = 1024;
    uint8_t data[NUM_OS_RANDOM_BYTES];
    bool overwritten[NUM_OS_RANDOM_BYTES] =
    {
    }; /* Tracks which bytes have been overwritten at least once */
    int num_overwritten;
    int tries = 0;
    /* Loop until all bytes have been overwritten at least once, or max number tries reached */
    do
    {
        memset(data, 0, NUM_OS_RANDOM_BYTES);
        GetOSRand(data);
        for (int x=0; x < NUM_OS_RANDOM_BYTES; ++x)
        {
            overwritten[x] |= (data[x] != 0);
        }

        num_overwritten = 0;
        for (int x=0; x < NUM_OS_RANDOM_BYTES; ++x)
        {
            if (overwritten[x])
                num_overwritten += 1;
        }

        tries += 1;
    }
    while (num_overwritten < NUM_OS_RANDOM_BYTES && tries < MAX_TRIES);
    if (num_overwritten != NUM_OS_RANDOM_BYTES)
        return false; /* If this failed, bailed out after too many tries */

    // Check that GetPerformanceCounter increases at least during a GetOSRand() call + 1ms sleep.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    uint64_t stop = GetPerformanceCounter();
    if (stop == start)
        return false;

    // We called GetPerformanceCounter. Use it as entropy.
    RAND_add((const unsigned char*)&start, sizeof(start), 1);
    RAND_add((const unsigned char*)&stop, sizeof(stop), 1);

    return true;
}

FastRandomContext::FastRandomContext(Random &random,
                                     bool fDeterministic)
    : random_(random)
    , requires_seed(!fDeterministic)
    , bytebuf_size(0)
    , bitbuf_size(0)
{
    if (!fDeterministic)
        return;
    uint256 seed;
    rng.SetKey(seed.begin(), 32);
}
