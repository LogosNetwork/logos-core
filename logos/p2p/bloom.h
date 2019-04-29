// Copyright (c) 2012-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOOM_H
#define BITCOIN_BLOOM_H

#include <stdint.h>
#include <vector>

class uint256;

/**
 * RollingBloomFilter is a probabilistic "keep track of most recently inserted" set.
 * Construct it with the number of items to keep track of, and a false-positive
 * rate. Unlike CBloomFilter, by default nTweak is set to a cryptographically
 * secure random value for you. Similarly rather than clear() the method
 * reset() is provided, which also changes nTweak to decrease the impact of
 * false-positives.
 *
 * contains(item) will always return true if item was one of the last N to 1.5*N
 * insert()'ed ... but may also return true for items that were not inserted.
 *
 * It needs around 1.8 bytes per element per factor 0.1 of false positive rate.
 * (More accurately: 3/(log(256)*log(2)) * log(1/fpRate) * nElements bytes)
 */
class CRollingBloomFilter
{
public:
    // A random bloom filter calls GetRand() at creation time.
    // Don't create global CRollingBloomFilter objects, as they may be
    // constructed before the randomizer is properly initialized.
    CRollingBloomFilter(const unsigned int nElements, const double nFPRate);

    void insert(const std::vector<unsigned char>& vKey);
    void insert(const uint256& hash);
    bool contains(const std::vector<unsigned char>& vKey) const;
    bool contains(const uint256& hash) const;

    void reset();

private:
    int nEntriesPerGeneration;
    int nEntriesThisGeneration;
    int nGeneration;
    std::vector<uint64_t> data;
    unsigned int nTweak;
    int nHashFuncs;
};

#endif // BITCOIN_BLOOM_H
