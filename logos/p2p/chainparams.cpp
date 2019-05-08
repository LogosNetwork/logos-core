// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <assert.h>
#include <chainparams.h>
#include <tinyformat.h>
#include <util.h>
#include <utilstrencodings.h>

const std::string CChainParams::MAIN = "main";
const std::string CChainParams::TESTNET = "test";
const std::string CChainParams::REGTEST = "regtest";

/**
 * Main network
 */
class CMainParams : public CChainParams
{
public:
    CMainParams()
    {
        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xf9;
        pchMessageStart[1] = 0xbe;
        pchMessageStart[2] = 0xb4;
        pchMessageStart[3] = 0xd9;
        nDefaultPort = MAINNET_DEFAULT_PORT;
        vFixedSeeds.clear(); //!< Mainnet mode doesn't have yet any fixed seeds.
        vSeeds.clear();      //!< Mainnet mode doesn't have yet any DNS seeds.
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams
{
public:
    CTestNetParams()
    {
        pchMessageStart[0] = 0x0b;
        pchMessageStart[1] = 0x11;
        pchMessageStart[2] = 0x09;
        pchMessageStart[3] = 0x07;
        nDefaultPort = TESTNET_DEFAULT_PORT;
        vFixedSeeds.clear(); //!< Testnet mode doesn't have yet any fixed seeds.
        vSeeds.clear();      //!< Testnet mode doesn't have yet any DNS seeds.
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams
{
public:
    CRegTestParams()
    {
        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        nDefaultPort = REGTEST_DEFAULT_PORT;
        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.
    }
};

std::shared_ptr<CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CChainParams::MAIN)
        return std::make_shared<CMainParams>();
    else if (chain == CChainParams::TESTNET)
        return std::make_shared<CTestNetParams>();
    else if (chain == CChainParams::REGTEST)
        return std::make_shared<CRegTestParams>();
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

std::shared_ptr<CChainParams> SelectParams(const std::string& network)
{
    return CreateChainParams(network);
}
