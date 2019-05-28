#pragma once
#include <logos/staking/staked_funds.hpp>
#include <logos/staking/thawing_funds.hpp>
#include <logos/blockstore.hpp>
#include <logos/staking/liability_manager.hpp>
#include <logos/staking/voting_power_manager.hpp>

const uint32_t THAWING_PERIOD = 42;

class StakingManager
{

    using BlockStore = logos::block_store;
    static std::shared_ptr<StakingManager> instance;
    public:
    static void SetInstance(BlockStore& store)
    {
        instance.reset(new StakingManager(store));
    }

    static std::shared_ptr<StakingManager> GetInstance()
    {
        return instance;
    }

    StakingManager(BlockStore& store) : _store(store), _liability_mgr(store), _voting_power_mgr(store) {}

    /* Set origins amount staked to amount
     * Direct the stake at target (target is either origin, or origins rep)
     *
     * Side Effects:
     * the balance of account_info may be modified, be sure to store account_info
     * in account_db after function returns
     * Voting power of target and origin's previous rep (if target is different)
     * are updated, including unlocked proxy for both
     */
    void Stake(
            AccountAddress const & origin,
            logos::account_info & account_info,
            Amount const & amount,
            AccountAddress const & target,
            uint32_t const & epoch,
            MDB_txn* txn);

    /* Returns true if origin can stake amount to target in epoch
     * @param origin account that owns funds to stake
     * @param info account_info associated with origin
     * @param amount amount to set stake to (can be less than current stake)
     * @param target account to stake to (either self or origin's rep)
     * @param epoch epoch number in which this action will occur
     * @param fee fee being charged for request
     * @returns true if action is valid
     * No side effects
     */
    bool Validate(
            AccountAddress const & origin,
            logos::account_info const & info,
            Amount const & amount,
            AccountAddress const & target,
            uint32_t const & epoch,
            Amount const & fee,
            MDB_txn* txn);


    /* @param origin - account that owns StakedFunds
     * @param funds - struct to populate with result
     * @param txn - lmdb transaction
     * @returns - true if found and funds argument was populated
     */
    bool GetCurrentStakedFunds(
            AccountAddress const & origin,
            StakedFunds & funds,
            MDB_txn* txn);


    /*
     * Returns all ThawingFunds in a vector
     */
    std::vector<ThawingFunds> GetThawingFunds(
            AccountAddress const & origin,
            MDB_txn* txn);

    /*
     * Removes any ThawingFunds that are expired at the start of or prior to
     * cur_epoch. Updates available_balance of info, and voting power of origins
     * rep (if origin has a rep)
     */
    void PruneThawing(
            AccountAddress const & origin,
            logos::account_info & info,
            uint32_t const & cur_epoch,
            MDB_txn* txn);

    /*
     * Returns amount of funds that would be pruned if PruneThawing was called
     * with the same parameters.
     * No side effects
     */
    Amount GetPruneableThawingAmount(
            AccountAddress const & origin,
            logos::account_info const & info,
            uint32_t const & cur_epoch,
            MDB_txn* txn);

    /*
     * Mark any funds that began thawing in epoch_created as frozen
     * (frozen funds have an expiration of 0, which means they never expire) 
     * Updates associated liabilities
     */
    void MarkThawingAsFrozen(
            AccountAddress const & origin,
            uint32_t const & epoch_created,
            MDB_txn* txn);

    /*
     * Set the expiration of any frozen funds to epoch_unfrozen + 42
     * Updates associated liabilities
     */
    void SetExpirationOfFrozen(
            AccountAddress const & origin,
            uint32_t const & epoch_unfrozen,
            MDB_txn* txn);




    private:

    //Returns number of thawing funds owned by origin
    uint8_t GetThawingCount(
            AccountAddress const & origin,
            uint32_t cur_epoch,
            MDB_txn*);

    /*
     * Creates new StakedFunds with amount = 0, and creates associated liability
     * Liabilities are consolidated based on epoch and target, so the associated
     * liability may have amount > 0.
     * Caller's responsibility to store returned funds in db
     */
    StakedFunds CreateStakedFunds(
            AccountAddress const & target,
            AccountAddress const & origin,
            MDB_txn* txn);

    /*
     * Creates new thawing funds with amount = 0, and creates associated liability
     * Liabilities are consolidated based on epoch and target, so the associated
     * liability may have amount > 0.
     * Caller's responsibility to store returned funds in db
     */
    ThawingFunds CreateThawingFunds(
            AccountAddress const & target,
            AccountAddress const & origin,
            uint32_t const & epoch_created,
            MDB_txn* txn);

    /*
     * Adds amount logos from origin's available_balance to output
     * Updates available_balance of account_info and associated voting power
     */
    void StakeAvailableFunds(
            StakedFunds & output,
            Amount const & amount,
            AccountAddress const & origin,
            logos::account_info & account_info,
            uint32_t const & epoch,
            MDB_txn* txn);

    /*
     * Updates funds.amount to amount, stores funds in db and updates associated
     * liability
     * If amount is 0, deletes funds and associated liability
     */
    void UpdateAmountAndStore(
            ThawingFunds & funds,
            AccountAddress const & origin,
            Amount const & new_amount,
            MDB_txn* txn);

    /*
     * Updates funds.amount to amount, stores funds in db and updates associated
     * liability
     * If amount is 0, deletes funds and associated liability
     */
    void UpdateAmountAndStore(
            StakedFunds & funds,
            AccountAddress const & origin,
            Amount const & new_amount,
            MDB_txn* txn);

   /* Extract attempts to move amount_to_extract funds from input to output
    * If extraction will create secondary liabilities that origin is not allowed to create
    * at this time, this function will do nothing
    * If input.amount < amount_to_extract, extract moves all of input.amount to output
    * @param input funds to extract from
    * @param output funds to extract into 
    * @param amount_to_extract amount to attempt to extract
    * @param origin account that owns the funds
    * @param epoch epoch during which to move funds
    * @return the amount extracted
    * Side effects :
    * creates secondary liability if necessary
    * calls UpdateAmountAndStore(...) for input
    *
    * Note:
    * Any changes to output are not stored in db
    * It is the callers responsibility to store output in db
    * Caller may want to call extract multiple
    * times with the same output and different inputs
    */
    template <typename T, typename R>
    Amount Extract(
            T & input,
            R & output,
            Amount amount_to_extract,
            AccountAddress const & origin,
            logos::account_info & info,
            uint32_t const & cur_epoch,
            MDB_txn* txn);



    /*
     * Executes func for each ThawingFund owned by origin
     * If func returns false, iteration stops
     * Note, thawing funds are stored and processed in reverse order of expiration
     * (furthest from thawing stored first)
     */
    void ProcessThawingFunds(
            AccountAddress const & origin,
            std::function<bool(ThawingFunds & funds)> func,
            MDB_txn* txn);



    /*
     * Executes func for each ThawingFund owned by origin
     * If func returns false, iteration stops
     * Note, thawing funds are stored and processed in reverse order of expiration
     * (furthest from thawing stored first)
     */
    void ProcessThawingFunds(
            AccountAddress const & origin,
            std::function<bool(ThawingFunds& funds, logos::store_iterator&)> func,
            MDB_txn* txn);


    // Stores StakedFunds in db, and updates amount of associated liability
    void Store(StakedFunds const & funds, AccountAddress const & origin, MDB_txn* txn);

    /* Stores ThawingFunds in db, and updates amount of associated liability
     * Note, thawing funds that have the same expiration and target are consolidated together
     * returns true if consolidation occured, or false if a new record was created in db
     */
    bool Store(ThawingFunds & funds, AccountAddress const & origin, MDB_txn* txn);

    // Deletes ThawingFunds from db. Caller's responsibility to delete associated liability
    void Delete(ThawingFunds const & funds, AccountAddress const & origin, MDB_txn* txn);

    // Deletes StakedFunds from db. Callers responsibility to delete associated liability
    void Delete(StakedFunds const & funds, AccountAddress const & origin, MDB_txn* txn);

    //Helper function used by Stake(...)
    //Changes target of staked funds and creates any secondary liabilities that are required
    //Creates new staked funds and attempts to extract amount_left from cur_stake into new staked funds
    //Amount_left is reduced by the amount extracted from cur_stake
    //updates voting power of affected reps
    //Returns newly created staked funds with proper target and liability hash
    //modifies cur_stake, amount_left and account_info
    StakedFunds ChangeTarget(
            AccountAddress const & origin,
            logos::account_info& account_info,
            uint32_t epoch,
            StakedFunds& cur_stake,
            AccountAddress const & new_target,
            Amount & amount_left,
            MDB_txn* txn);

    /* Helper function used by Stake(...)
     * Reduce's origin's current stake by amount_to_thaw
     * updates voting power of affected reps
     * modifies cur_stake and account_info
     */
    void ReduceStake(
            AccountAddress const & origin,
            logos::account_info& account_info,
            uint32_t epoch,
            StakedFunds& cur_stake,
            Amount const & amount_to_thaw,
            MDB_txn* txn);

    /*
     * Helper function used by ChangeTarget(...) and ReduceStake(...)
     * Moves amount_to_thaw funds from cur_stake to the thawing state
     * Stores ThawingFunds in db
     * Modifies cur_stake and account_info
     */
    void BeginThawing(
            AccountAddress const & origin,
            logos::account_info& account_info,
            uint32_t epoch,
            StakedFunds& cur_stake,
            Amount amount_to_thaw,
            MDB_txn* txn);


    private:
    Log _log;
    LiabilityManager _liability_mgr;
    VotingPowerManager _voting_power_mgr;
    BlockStore& _store;

    //TODO need a better pattern to test the private methods of this class
    //The private methods should not be called outside of staking manager
    //but their logic needs to be tested
    friend class Staking_Manager_Stake_Test;
    friend class Staking_Manager_StakeEpochTransition_Test;
    friend class Staking_Manager_Validate_Test;
    friend class Staking_Manager_Thawing_Test;
    friend class Staking_Manager_Frozen_Test;
    friend class Staking_Manager_Extract_Test;

};
