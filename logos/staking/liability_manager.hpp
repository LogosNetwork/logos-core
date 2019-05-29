#pragma once

#include <logos/blockstore.hpp>
#include <logos/staking/liability.hpp>

class LiabilityManager
{

    using BlockStore = logos::block_store;
    public:
    LiabilityManager(BlockStore& store) : _store(store) {}

    /* Creates a liability for amount that expires in expiration_epoch
    * Note, liabilities are consolidated based on target, source and expiration_epoch
    * If a liability already exists with the same target, source and expiration_epoch
    * amount is added to the existing liability amount
    * Returns a hash to the liability
    */
    LiabilityHash CreateExpiringLiability(
            AccountAddress const & target,
            AccountAddress const & source,
            Amount const & amount,
            uint32_t const & expiration_epoch,
            MDB_txn* txn);

    /* Creates a liability for amount that does not expire
    * Note, liabilities are consolidated based on target, source and expiration_epoch
    * If a liability already exists with the same target, source and expiration_epoch
    * amount is added to the existing liability amount
    * Returns a hash to the liability
    */
    LiabilityHash CreateUnexpiringLiability(
            AccountAddress const & target,
            AccountAddress const & source,
            Amount const & amount,
            MDB_txn* txn);

    /* Creates a secondary liability if possible, that expires in expiration_epoch
     * Secondary liabilities should be up to date prior to calling this function
     * If a liability already exists with the same target, source and expiration_epoch
     * amount is added to the existing liability amount
     * returns true if secondary liability was created (or added to) and false otherwisse
     * Note, all secondary liabilities for an account must have the same target
     */
    bool CreateSecondaryLiability(
            AccountAddress const & target,
            AccountAddress const & source,
            Amount const & amount,
            uint32_t const & expiration_epoch,
            logos::account_info & info,
            uint32_t const & cur_epoch,
            MDB_txn* txn);

    /*
     * Updates the amount of liability with hash = hash
     */
    void UpdateLiabilityAmount(
            LiabilityHash const & hash,
            Amount const & amount,
            MDB_txn* txn);

    /*
     * Deletes liability. Does not remove hash from secondary_liabilities_db 
     */
    void DeleteLiability(LiabilityHash const & hash,
            MDB_txn* txn);

    /*
     * Returns hashes of all liabilities where rep is a target
     */
    std::vector<LiabilityHash> GetRepLiabilities(
            AccountAddress const & rep,
            MDB_txn* txn);

    /*
     * Returns hashes of all secondary liabilities where origin is source
     */
    std::vector<LiabilityHash> GetSecondaryLiabilities(
            AccountAddress const & origin,
            MDB_txn* txn);

    /* Returns liability associated with hash
     */
    Liability Get(LiabilityHash const & hash, MDB_txn* txn);
    /*
     * Returns true if liability with hash exists
     */
    bool Exists(LiabilityHash const & hash, MDB_txn* txn);

    /*
     * Returns true if source account can create a secondary liability
     * with target in cur_epoch
     * All secondary liabilities for which an account is a source must have the
     * same target
     */
    bool CanCreateSecondaryLiability(
            AccountAddress const & target,
            AccountAddress const & source,
            uint32_t const & cur_epoch,
            MDB_txn* txn);

    /*
     * Removes any secondary liabilities that have expired by cur_epoch
     * Updates info.secondary_liabilities_updated 
     */
    void PruneSecondaryLiabilities(
            AccountAddress const & origin,
            logos::account_info & info,
            uint32_t const & cur_epoch,
            MDB_txn* txn);

    private:
    /* Stores l in master_liability_db and rep_liability_db
     * Consolidates liabilities with the same target, source and expiration_epoch
     */
    LiabilityHash Store(Liability const & l, MDB_txn* txn);

    /*
     * Gets hashes of all liabilities for which account is a source
     * stored in the db that dbi points to
     */
    std::vector<LiabilityHash> GetHashes(
            AccountAddress const & account,
            MDB_dbi BlockStore::*dbi,
            MDB_txn* txn);


    private:
    BlockStore& _store;
    Log _log;
};
