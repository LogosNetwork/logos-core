// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAINPARAMS_H
#define BITCOIN_CHAINPARAMS_H

#include <chainparamsbase.h>
#if 0
#include <consensus/params.h>
#include <primitives/block.h>
#endif
#include <protocol.h>

#include <memory>
#include <vector>

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};

#if 0
typedef std::map<int, uint256> MapCheckpoints;

struct CCheckpointData {
    MapCheckpoints mapCheckpoints;
};

/**
 * Holds various statistics on transactions within a chain. Used to estimate
 * verification progress during chain sync.
 *
 * See also: CChainParams::TxData, GuessVerificationProgress.
 */
struct ChainTxData {
    int64_t nTime;    //!< UNIX timestamp of last known number of transactions
    int64_t nTxCount; //!< total number of transactions between genesis and that timestamp
    double dTxRate;   //!< estimated number of transactions per second after that timestamp
};
#endif

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
#if 0
    enum Base58Type {
        PUBKEY_ADDRESS,
        SCRIPT_ADDRESS,
        SECRET_KEY,
        EXT_PUBLIC_KEY,
        EXT_SECRET_KEY,

        MAX_BASE58_TYPES
    };

    const Consensus::Params& GetConsensus() const { return consensus; }
#endif
    const CMessageHeader::MessageStartChars& MessageStart() const { return pchMessageStart; }
    int GetDefaultPort() const { return nDefaultPort; }
#if 0
    const CBlock& GenesisBlock() const { return genesis; }
    /** Default value for -checkmempool and -checkblockindex argument */
    bool DefaultConsistencyChecks() const { return fDefaultConsistencyChecks; }
    /** Policy: Filter transactions that do not match well-defined patterns */
    bool RequireStandard() const { return fRequireStandard; }
    uint64_t PruneAfterHeight() const { return nPruneAfterHeight; }
    /** Make miner stop after a block is found. In RPC, don't return until nGenProcLimit blocks are generated */
    bool MineBlocksOnDemand() const { return fMineBlocksOnDemand; }
    /** Return the BIP70 network string (main, test or regtest) */
    std::string NetworkIDString() const { return strNetworkID; }
    /** Return true if the fallback fee is by default enabled for this network */
    bool IsFallbackFeeEnabled() const { return m_fallback_fee_enabled; }
#endif
    /** Return the list of hostnames to look up for DNS seeds */
    const std::vector<std::string>& DNSSeeds() const { return vSeeds; }
#if 0
    const std::vector<unsigned char>& Base58Prefix(Base58Type type) const { return base58Prefixes[type]; }
    const std::string& Bech32HRP() const { return bech32_hrp; }
#endif
    const std::vector<SeedSpec6>& FixedSeeds() const { return vFixedSeeds; }
#if 0
    const CCheckpointData& Checkpoints() const { return checkpointData; }
    const ChainTxData& TxData() const { return chainTxData; }
    void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout);
#endif
protected:
    CChainParams() {}
#if 0
    Consensus::Params consensus;
#endif
    CMessageHeader::MessageStartChars pchMessageStart;
    int nDefaultPort;
#if 0
    uint64_t nPruneAfterHeight;
#endif
    std::vector<std::string> vSeeds;
#if 0
    std::vector<unsigned char> base58Prefixes[MAX_BASE58_TYPES];
    std::string bech32_hrp;
    std::string strNetworkID;
    CBlock genesis;
#endif
    std::vector<SeedSpec6> vFixedSeeds;
#if 0
    bool fDefaultConsistencyChecks;
    bool fRequireStandard;
    bool fMineBlocksOnDemand;
    CCheckpointData checkpointData;
    ChainTxData chainTxData;
    bool m_fallback_fee_enabled;
#endif
};

/**
 * Creates and returns a std::unique_ptr<CChainParams> of the chosen chain.
 * @returns a CChainParams* of the chosen chain.
 * @throws a std::runtime_error if the chain is not supported.
 */
std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain);

/**
 * Return the currently selected parameters. This won't change after app
 * startup, except for unit tests.
 */
const CChainParams &Params();

/**
 * Sets the params returned by Params() to those for the given BIP70 chain name.
 * @throws std::runtime_error when the chain is not supported.
 */
void SelectParams(const std::string& chain);

#if 0
/**
 * Allows modifying the Version Bits regtest parameters.
 */
void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout);
#endif

#endif // BITCOIN_CHAINPARAMS_H
