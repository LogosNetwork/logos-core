#pragma once
#include <logos/staking/staked_funds.hpp>
#include <logos/staking/thawing_funds.hpp>
#include <logos/blockstore.hpp>
#include <logos/staking/liability_manager.hpp>
#include <logos/staking/voting_power_manager.hpp>

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

    StakedFunds CreateStakedFunds(
            AccountAddress const & target,
            AccountAddress const & origin,
            MDB_txn* txn);

    ThawingFunds CreateThawingFunds(
            AccountAddress const & target,
            AccountAddress const & origin,
            uint32_t const & epoch_created,
            MDB_txn* txn);

    void StakeAvailableFunds(
            StakedFunds & output,
            Amount const & amount,
            AccountAddress const & origin,
            logos::account_info & account_info,
            uint32_t const & epoch,
            MDB_txn* txn);


    void UpdateAmount(
            ThawingFunds & funds,
            AccountAddress const & origin,
            Amount const & amount,
            MDB_txn* txn);


    void UpdateAmount(
            StakedFunds & funds,
            AccountAddress const & origin,
            Amount const & amount,
            MDB_txn* txn);

    template <typename T, typename R>
    Amount Extract(
            T & input,
            R & output,
            Amount const & amount,
            AccountAddress const & origin,
            uint32_t const & epoch,
            MDB_txn* txn);

    void Stake(
            AccountAddress const & origin,
            logos::account_info & account_info,
            Amount const & amount,
            AccountAddress const & target,
            uint32_t const & epoch,
            MDB_txn* txn);

    bool Validate(
            AccountAddress const & origin,
            Amount const & amount,
            AccountAddress const & target,
            uint32_t const & epoch,
            MDB_txn* txn);

    StakedFunds GetCurrentStakedFunds(
            AccountAddress const & origin,
            MDB_txn* txn);

    std::vector<ThawingFunds> GetThawingFunds(
            AccountAddress const & origin,
            MDB_txn* txn);

    void IterateThawingFunds(
            AccountAddress const & origin,
            std::function<bool(ThawingFunds & funds)> func,
            MDB_txn* txn);

    void PruneThawing(
            AccountAddress const & origin,
            uint32_t const & cur_epoch,
            MDB_txn* txn);


    void Store(StakedFunds const & funds, AccountAddress const & origin, MDB_txn* txn);
    void Store(ThawingFunds & funds, AccountAddress const & origin, MDB_txn* txn);
    void Delete(ThawingFunds const & funds, AccountAddress const & origin, MDB_txn* txn);
    void Delete(StakedFunds const & funds, AccountAddress const & origin, MDB_txn* txn);

    private:
    Log _log;
    LiabilityManager _liability_mgr;
    VotingPowerManager _voting_power_mgr;
    BlockStore& _store;
};
