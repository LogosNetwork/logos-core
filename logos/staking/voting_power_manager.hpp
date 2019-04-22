#pragma once

#include <logos/blockstore.hpp>
#include <logos/staking/voting_power.hpp>

// integer between 0 and 100. Represents a percent
const uint8_t DILUTION_FACTOR = 25;

//TODO sensible return values for member functions
//TODO does this class really belong in logos/staking?
class VotingPowerManager
{
    using BlockStore = logos::block_store;

    //TODO does this need to be a pointer?
    static std::shared_ptr<VotingPowerManager> instance;
    public:

    VotingPowerManager(BlockStore& store) : _store(store) {}

    static void Init(BlockStore& store)
    {
        instance.reset(new VotingPowerManager(store));
    }

    static std::shared_ptr<VotingPowerManager> Get()
    {
        return instance;
    }

    bool SubtractLockedProxied(
            AccountAddress const & rep,
            Amount const & amount,
            uint32_t const & epoch_number,
            MDB_txn* txn);


    bool AddLockedProxied(
            AccountAddress const & rep,
            Amount const & amount,
            uint32_t const & epoch_number,
            MDB_txn* txn);

    bool SubtractUnlockedProxied(
            AccountAddress const & rep,
            Amount const & amount,
            uint32_t const & epoch_number,
            MDB_txn* txn);

    bool AddUnlockedProxied(
            AccountAddress const & rep,
            Amount const & amount,
            uint32_t const & epoch_number,
            MDB_txn* txn);


    bool SubtractSelfStake(
            AccountAddress const & rep,
            Amount const & amount,
            uint32_t const & epoch_number,
            MDB_txn* txn);


    bool AddSelfStake(
            AccountAddress const & rep,
            Amount const & amount,
            uint32_t const & epoch_number,
            MDB_txn* txn);

    Amount GetCurrentVotingPower(
            AccountAddress const & rep,
            uint32_t const & epoch_number,
            MDB_txn* txn);

    //returns true if info was found
    bool GetVotingPowerInfo(
            AccountAddress const & rep,
            VotingPowerInfo& info,
            MDB_txn* txn);

    void TryPrune(
            AccountAddress const & rep,
            MDB_txn* txn);

    bool CanPrune(
            AccountAddress const & rep,
            VotingPowerInfo const & info,
            MDB_txn* txn);


    void UpdateBalance(
            logos::Account* account,
            Amount const & new_balance,
            uint32_t const & epoch,
            MDB_txn* txn);

    private:
    void StoreOrPrune(
            AccountAddress const & rep,
            VotingPowerInfo& info,
            MDB_txn* txn);


    BlockStore& _store;
    Log _log;
};
