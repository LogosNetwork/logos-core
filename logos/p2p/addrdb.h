// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_ADDRDB_H
#define BITCOIN_ADDRDB_H

#include <string>
#include <map>
#include <serialize.h>
#include <chainparams.h>
#include <p2p.h>

class CSubNet;
class CAddrMan;

typedef enum BanReason
{
    BanReasonUnknown          = 0,
    BanReasonNodeMisbehaving  = 1,
    BanReasonManuallyAdded    = 2
} BanReason;

class CBanEntry
{
public:
    static constexpr int    CURRENT_VERSION = 1;
    int                     nVersion;
    int64_t                 nCreateTime;
    int64_t                 nBanUntil;
    uint8_t                 banReason;

    CBanEntry()
    {
        SetNull();
    }

    explicit CBanEntry(int64_t nCreateTimeIn)
    {
        SetNull();
        nCreateTime = nCreateTimeIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(this->nVersion);
        READWRITE(nCreateTime);
        READWRITE(nBanUntil);
        READWRITE(banReason);
    }

    void SetNull()
    {
        nVersion = CBanEntry::CURRENT_VERSION;
        nCreateTime = 0;
        nBanUntil = 0;
        banReason = BanReasonUnknown;
    }

    std::string banReasonToString() const
    {
        switch (banReason)
        {
        case BanReasonNodeMisbehaving:
            return "node misbehaving";
        case BanReasonManuallyAdded:
            return "manually added";
        default:
            return "unknown";
        }
    }
};

typedef std::map<CSubNet, CBanEntry> banmap_t;

/** Access to the (IP) address database (peers.dat) */
class CAddrDB
{
private:
    MDB_env *                       env;
    MDB_dbi                         dbi;
    BCLog::Logger &                 logger_;
    std::shared_ptr<CChainParams>   params;
public:
    CAddrDB(struct p2p_config &config,
            BCLog::Logger &logger,
            std::shared_ptr<CChainParams> paramsIn)
        : env(config.lmdb_env)
        , dbi(config.lmdb_dbi)
        , logger_(logger)
        , params(paramsIn)
    {
    }
    bool Write(const CAddrMan& addr);
    bool Read(CAddrMan& addr);
};

/** Access to the banlist database (banlist.dat) */
class CBanDB
{
private:
    MDB_env *                       env;
    MDB_dbi                         dbi;
    BCLog::Logger &                 logger_;
    std::shared_ptr<CChainParams>   params;
public:
    CBanDB(struct p2p_config &config,
           BCLog::Logger &logger,
           std::shared_ptr<CChainParams> paramsIn)
        : env(config.lmdb_env)
        , dbi(config.lmdb_dbi)
        , logger_(logger)
        , params(paramsIn)
    {
    }
    bool Write(const banmap_t& banSet);
    bool Read(banmap_t& banSet);
};

#endif // BITCOIN_ADDRDB_H
