// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAINPARAMS_H
#define BITCOIN_CHAINPARAMS_H

#include <memory>
#include <vector>
#include <string>
#include <protocol.h>

struct SeedSpec6
{
    uint8_t addr[16];
    uint16_t port;
};

/**
 * CChainParams defines various tweakable parameters of a given instance of the
 * Bitcoin system. There are three: the main network on which people trade goods
 * and services, the public test network which gets reset from time to time and
 * a regression test mode which is intended for private networks only. It has
 * minimal difficulty to ensure that blocks can be found instantly.
 */
class CChainParams
{
public:
    static const std::string MAIN;
    static const std::string TESTNET;
    static const std::string REGTEST;

    const CMessageHeader::MessageStartChars& MessageStart() const
    {
        return pchMessageStart;
    }

    int GetDefaultPort() const
    {
        return nDefaultPort;
    }

    /** Return the list of hostnames to look up for DNS seeds */
    const std::vector<std::string>& DNSSeeds() const
    {
        return vSeeds;
    }

    const std::vector<SeedSpec6>& FixedSeeds() const
    {
        return vFixedSeeds;
    }

protected:
    CChainParams()
    {
    }

    CMessageHeader::MessageStartChars pchMessageStart;
    int nDefaultPort;
    std::vector<std::string> vSeeds;
    std::vector<SeedSpec6> vFixedSeeds;
};

/**
 * Creates and returns a std::unique_ptr<CChainParams> of the chosen chain.
 * @returns a CChainParams* of the chosen chain.
 * @throws a std::runtime_error if the chain is not supported.
 */
std::shared_ptr<CChainParams> CreateChainParams(const std::string& chain);

/**
 * Sets the params returned by Params() to those for the given BIP70 chain name.
 * @throws std::runtime_error when the chain is not supported.
 */
std::shared_ptr<CChainParams> SelectParams(const std::string& chain);

#endif // BITCOIN_CHAINPARAMS_H
