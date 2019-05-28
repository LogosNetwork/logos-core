// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "../../lmdb/libraries/liblmdb/lmdb.h"
#include <config/bitcoin-config.h>
#include <addrdb.h>
#include <addrman.h>
#include <chainparams.h>
#include <hash.h>
#include <random.h>
#include <streams.h>
#include <tinyformat.h>
#include <util.h>

namespace
{

template <typename Stream, typename Data>
bool SerializeDB(Stream& stream, const Data& data, BCLog::Logger &logger_, std::shared_ptr<CChainParams> params)
{
    // Write and commit header, data
    try
    {
        CHashWriter hasher(SER_DISK, CLIENT_VERSION);
        stream << params->MessageStart() << data;
        hasher << params->MessageStart() << data;
        stream << hasher.GetHash();
    }
    catch (const std::exception& e)
    {
        return error(logger_, "%s: Serialize or I/O error - %s", __func__, e.what());
    }

    return true;
}

template <typename Data>
bool SerializeLMDB(const std::string &prefix, MDB_env *env, MDB_dbi dbi, const Data& data, BCLog::Logger &logger_, std::shared_ptr<CChainParams> params)
{
    CDataStream s(SER_DISK, CLIENT_VERSION);
    MDB_txn *txn;
    int err;

    // Serialize
    if (!SerializeDB(s, data, logger_, params))
        return error(logger_, "%s: Failed to serialize %s data", __func__, prefix.c_str());

    MDB_val key = { prefix.size(), (void *)prefix.c_str() }, value = { s.size(), s.data() };

    if ((err = mdb_txn_begin(env, 0, 0, &txn)))
        return error(logger_, "%s: Failed to open %s write transaction, error %d", __func__, prefix.c_str(), err);

    if ((err = mdb_put(txn, dbi, &key, &value, 0)))
    {
        mdb_txn_abort(txn);
        return error(logger_, "%s: Failed to put %s data, error %d", __func__, prefix.c_str(), err);
    }

    if ((err = mdb_txn_commit(txn)))
    {
        mdb_txn_abort(txn);
        return error(logger_, "%s: Failed to commit %s transaction, error %d", __func__, prefix.c_str(), err);
    }

    return true;
}

template <typename Stream, typename Data>
bool DeserializeDB(Stream& stream, Data& data, BCLog::Logger &logger_, std::shared_ptr<CChainParams> params, bool fCheckSum = true)
{
    try
    {
        CHashVerifier<Stream> verifier(&stream);
        // de-serialize file header (network specific magic number) and ..
        unsigned char pchMsgTmp[4];
        verifier >> pchMsgTmp;
        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, params->MessageStart(), sizeof(pchMsgTmp)))
            return error(logger_, "%s: Invalid network magic number", __func__);

        // de-serialize data
        verifier >> data;

        // verify checksum
        if (fCheckSum)
        {
            uint256 hashTmp;
            stream >> hashTmp;
            if (hashTmp != verifier.GetHash())
                return error(logger_, "%s: Checksum mismatch, data corrupted", __func__);
        }
    }
    catch (const std::exception& e)
    {
        return error(logger_, "%s: Deserialize or I/O error - %s", __func__, e.what());
    }

    return true;
}

template <typename Data>
bool DeserializeLMDB(const std::string &prefix, MDB_env *env, MDB_dbi dbi, Data& data, BCLog::Logger &logger_, std::shared_ptr<CChainParams> params)
{
    MDB_txn *txn;
    int err;
    MDB_val key = { prefix.size(), (void *)prefix.c_str() }, value;

    if ((err = mdb_txn_begin(env, 0, MDB_RDONLY, &txn)))
        return error(logger_, "%s: Failed to open %s read transaction, error %d", __func__, prefix.c_str(), err);

    if ((err = mdb_get(txn, dbi, &key, &value)))
    {
        mdb_txn_abort(txn);
        return error(logger_, "%s: Failed to get %s data, error %d", __func__, prefix.c_str(), err);
    }

    CDataStream s((const char *)value.mv_data, (const char *)value.mv_data + value.mv_size, SER_DISK, CLIENT_VERSION);

    // Deserialize
    if (!DeserializeDB(s, data, logger_, params))
    {
        mdb_txn_abort(txn);
        return error(logger_, "%s: Failed to deserialize %s data", __func__, prefix.c_str());
    }

    mdb_txn_abort(txn);

    return true;
}

}

bool CBanDB::Write(const banmap_t& banSet)
{
    return SerializeLMDB("banlist", env, dbi, banSet, logger_, params);
}

bool CBanDB::Read(banmap_t& banSet)
{
    return DeserializeLMDB("banlist", env, dbi, banSet, logger_, params);
}

bool CAddrDB::Write(const CAddrMan& addr)
{
    return SerializeLMDB("peers", env, dbi, addr, logger_, params);
}

bool CAddrDB::Read(CAddrMan& addr)
{
    return DeserializeLMDB("peers", env, dbi, addr, logger_, params);
}

