#pragma once

#include <logos/blockstore.hpp>
#include <logos/staking/liability.hpp>

class LiabilityManager
{

    using BlockStore = logos::block_store;
    public:
    LiabilityManager(BlockStore& store) : _store(store) {}

    LiabilityHash CreateExpiringLiability(
            AccountAddress const & target,
            AccountAddress const & source,
            Amount const & amount,
            uint32_t const & expiration_epoch,
            MDB_txn* txn);

    LiabilityHash CreateUnexpiringLiability(
            AccountAddress const & target,
            AccountAddress const & source,
            Amount const & amount,
            MDB_txn* txn);

    bool CreateSecondaryLiability(
            AccountAddress const & target,
            AccountAddress const & source,
            Amount const & amount,
            uint32_t const & expiration_epoch,
            MDB_txn* txn);

    void UpdateLiabilityAmount(
            LiabilityHash const & hash,
            Amount const & amount,
            MDB_txn* txn);

    void DeleteLiability(LiabilityHash const & hash,
            MDB_txn* txn);

    std::vector<LiabilityHash> GetRepLiabilities(
            AccountAddress const & rep,
            MDB_txn* txn);

    std::vector<LiabilityHash> GetSecondaryLiabilities(
            AccountAddress const & origin,
            MDB_txn* txn);

    Liability Get(LiabilityHash const & hash, MDB_txn* txn);
    bool Exists(LiabilityHash const & hash, MDB_txn* txn);

    bool CanCreateSecondaryLiability(
            AccountAddress const & target,
            AccountAddress const & source,
            logos::account_info & info,
            uint32_t const & cur_epoch,
            MDB_txn* txn);

    void PruneSecondaryLiabilities(
            AccountAddress const & origin,
            logos::account_info & info,
            uint32_t const & cur_epoch,
            MDB_txn* txn);

    private:
    LiabilityHash Store(Liability const & l, MDB_txn* txn);

    std::vector<LiabilityHash> GetHashes(
            AccountAddress const & account,
            MDB_dbi BlockStore::*dbi,
            MDB_txn* txn);


    private:
    BlockStore& _store;
    Log _log;
};
