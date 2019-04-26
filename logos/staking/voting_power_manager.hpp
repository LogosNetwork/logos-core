#pragma once

#include <logos/blockstore.hpp>
#include <logos/staking/voting_power.hpp>
#include <boost/optional.hpp>

// integer between 0 and 100. Represents a percent
const uint8_t DILUTION_FACTOR = 25;

//TODO sensible return values for member functions
//TODO does this class really belong in logos/staking?
class VotingPowerManager
{
    using BlockStore = logos::block_store;

    //Note, VotingPowerManager is not a singleton, but contains an instance
    //for convenience. Some areas of the code do not have a reference to
    //blockstore to create a VotingPowerManager. This instance
    //is created when BlockStore is constructed. A client can use this instance
    //or can create their own; the behavior is identical.
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

    //returns true if info was found
    bool GetVotingPowerInfo(
            AccountAddress const & rep,
            uint32_t const & epoch,
            VotingPowerInfo& info,
            MDB_txn* txn);

    bool TransitionIfNecessary(
            VotingPowerInfo& info,
            uint32_t const & epoch,
            AccountAddress const & rep,
            MDB_txn* txn);


    void TryPrune(
            AccountAddress const & rep,
            MDB_txn* txn);

    bool CanPrune(
            AccountAddress const & rep,
            VotingPowerInfo const & info,
            MDB_txn* txn);

    boost::optional<AccountAddress> GetRep(logos::account_info const & info, MDB_txn* txn);

    private:

    enum class DiffType
    {
    ADD = 1,
    SUBTRACT = 2
    };
    void Modify(
            VotingPowerInfo& info,
            AccountAddress const & account,
            Amount VotingPowerSnapshot::*snapshot_member,
            uint32_t const & epoch,
            Amount const & diff,
            DiffType diff_type,
            MDB_txn* txn);


    void StoreOrPrune(
            AccountAddress const & rep,
            VotingPowerInfo& info,
            MDB_txn* txn);


    BlockStore& _store;
    Log _log;
};
