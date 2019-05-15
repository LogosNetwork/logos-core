#pragma once

#include <logos/blockstore.hpp>
#include <logos/staking/voting_power.hpp>
#include <boost/optional.hpp>

// integer between 0 and 100. Represents a percent
const uint8_t DILUTION_FACTOR = 25;

class VotingPowerManager
{
    using BlockStore = logos::block_store;

    //Note, VotingPowerManager is not a singleton, but contains an instance
    //for convenience. Some areas of the codebase do not have a reference to
    //blockstore to create a VotingPowerManager. This instance
    //is created when BlockStore is constructed. A client can use this instance
    //or can create their own; the behavior is identical.
    static std::shared_ptr<VotingPowerManager> instance;
    public:

    VotingPowerManager(BlockStore& store) : _store(store) {}

    static void SetInstance(BlockStore& store)
    {
        instance.reset(new VotingPowerManager(store));
    }

    static std::shared_ptr<VotingPowerManager> GetInstance()
    {
        return instance;
    }

    /* Note, VotingPowerInfo must already exist for rep
     */
    bool SubtractLockedProxied(
            AccountAddress const & rep,
            Amount const & amount,
            uint32_t const & epoch_number,
            MDB_txn* txn);

    /* Note, VotingPowerInfo must already exist for rep
     */
    bool AddLockedProxied(
            AccountAddress const & rep,
            Amount const & amount,
            uint32_t const & epoch_number,
            MDB_txn* txn);

    /* Note, VotingPowerInfo must already exist for rep
     */
    bool SubtractUnlockedProxied(
            AccountAddress const & rep,
            Amount const & amount,
            uint32_t const & epoch_number,
            MDB_txn* txn);

    /* Note, VotingPowerInfo must already exist for rep
     */
    bool AddUnlockedProxied(
            AccountAddress const & rep,
            Amount const & amount,
            uint32_t const & epoch_number,
            MDB_txn* txn);

    /* Note, VotingPowerInfo does not need to already exist for rep
     */
    bool SubtractSelfStake(
            AccountAddress const & rep,
            Amount const & amount,
            uint32_t const & epoch_number,
            MDB_txn* txn);

    /* Note, VotingPowerInfo does not need to already exist for rep
     */
    bool AddSelfStake(
            AccountAddress const & rep,
            Amount const & amount,
            uint32_t const & epoch_number,
            MDB_txn* txn);

    /* Returns the voting power of rep for epoch_number
     */
    Amount GetCurrentVotingPower(
            AccountAddress const & rep,
            uint32_t const & epoch_number,
            MDB_txn* txn);

    /*
     * Returns locked_proxied + self_stake of rep for epoch_number
     */
   Amount GetCurrentTotalStake(
           AccountAddress const & rep,
           uint32_t const & epoch_number,
           MDB_txn* txn);

    //returns true if info was found
    //Note, this function does not take in an epoch_number
    //and does not update voting power based on epoch_number
    //this functions should only be used for debugging and testing
    bool GetVotingPowerInfo(
            AccountAddress const & rep,
            VotingPowerInfo& info,
            MDB_txn* txn);

    //returns true if info was found
    bool GetVotingPowerInfo(
            AccountAddress const & rep,
            uint32_t const & epoch,
            VotingPowerInfo& info,
            MDB_txn* txn);

    /*
     * Transitions info to the next epoch, if epoch > info.epoch_modified
     * stores self_stake in candidacy_db if record in candidacy_db is stale
     * calls HandleFallback if transition occurs
     * returns true if transition occurs 
     */
    bool TransitionIfNecessary(
            VotingPowerInfo& info,
            uint32_t const & epoch,
            AccountAddress const & rep,
            MDB_txn* txn);

    /*
     * If voting power is being transitioned to the next epoch for rep,
     * but rep has not voted in the previous epoch yet, store info in 
     * voting_power_fallback_db in case rep is currently casting a vote
     * Otherwise, if rep has voted, delete the record in voting_power_fallback_db
     * (if one exists)
     */
    void HandleFallback(
            VotingPowerInfo const & info,
            AccountAddress const & rep,
            uint32_t epoch,
            MDB_txn* txn);

    /* Prunes voting power for rep if rep is no longer rep
     * and if total voting power is 0
     */
    void TryPrune(
            AccountAddress const & rep,
            MDB_txn* txn);

    /* Returns true if total voting power in info is 0
     * and rep is no longer a rep
     */
    bool CanPrune(
            AccountAddress const & rep,
            VotingPowerInfo const & info,
            MDB_txn* txn);

    /* Returns AccountAddress of rep associated with info
     * If account has no rep, or is rep themselves, returns empty optional
     */
    boost::optional<AccountAddress> GetRep(logos::account_info const & info, MDB_txn* txn);

    private:

    /*
     * Helper function
     * Adds or subtracts diff from appropriate member of info
     */
    void Modify(
            VotingPowerInfo& info,
            AccountAddress const & account,
            Amount VotingPowerSnapshot::*snapshot_member,
            uint32_t const & epoch,
            Amount const & diff,
            std::function<Amount&(Amount&, Amount const &)> func,
            MDB_txn* txn);



    /* Stores info in voting_power_db with rep as key if total power is > 0
     * else deletes key-value pair for rep in voting power db
     */
    void StoreOrPrune(
            AccountAddress const & rep,
            VotingPowerInfo& info,
            MDB_txn* txn);


    BlockStore& _store;
    Log _log;
};
