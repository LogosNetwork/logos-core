#pragma once

#include <logos/blockstore.hpp>
#include <logos/staking/voting_power.hpp>



//TODO sensible return values for member functions
//TODO does this class really belong in logos/staking?
class VotingPowerManager
{
    using BlockStore = logos::block_store;

    public:

    VotingPowerManager(BlockStore& store) : _store(store) {}

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

    void TryPrune(
            AccountAddress const & rep,
            MDB_txn* txn);


    private:
    void StoreOrPrune(
            AccountAddress const & rep,
            VotingPowerInfo& info,
            MDB_txn* txn);

    bool CanPrune(
            AccountAddress const & rep,
            VotingPowerInfo const & info,
            MDB_txn* txn);

    BlockStore& _store;
    Log _log;
};
