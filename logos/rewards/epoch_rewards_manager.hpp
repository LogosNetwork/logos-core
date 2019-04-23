#pragma once
#include <logos/common.hpp>
#include <logos/blockstore.hpp>
#include <logos/rewards/epoch_rewards.hpp>


//TODO include rep address in this struct?
struct RepEpochInfo
{
    uint8_t levy_percentage;
    uint32_t epoch_number;
    //includes locked proxied and self stake
    Amount total_stake;

    RepEpochInfo(uint8_t levy, uint32_t epoch, Amount stake)
        : levy_percentage(levy)
        , epoch_number(epoch)
        , total_stake(stake)
    {}
};


const size_t EPOCH_REWARDS_KEYSIZE = 36;

class EpochRewardsManager
{
    using BlockStore = logos::block_store;

    public:

    EpochRewardsManager(BlockStore & store);

    void Init(
            AccountAddress const & rep_address,
            RepEpochInfo const & rep_epoch_info,
            MDB_txn * txn);

    bool SetTotalReward(
            AccountAddress const & rep_address,
            uint32_t const & epoch_number,
            Amount const & total_reward,
            MDB_txn* txn);

    bool HarvestReward(
            AccountAddress const & rep_address,
            uint32_t const & epoch_number,
            Amount const & harvest_amount,
            MDB_txn* txn);

    EpochRewardsInfo GetEpochRewardsInfo(
            AccountAddress const & rep_address,
            uint32_t const & epoch_number,
            MDB_txn* txn);

    EpochRewardsInfo GetEpochRewardsInfo(
            std::array<uint8_t, EPOCH_REWARDS_KEYSIZE>& key,
            MDB_txn* txn);

    //TODO create a typedef for the array type?
    std::array<uint8_t, EPOCH_REWARDS_KEYSIZE> MakeKey(
            AccountAddress const & account,
            uint32_t const & epoch);

    GlobalEpochRewardsInfo GetGlobalEpochRewardsInfo(
            uint32_t const & epoch_number,
            MDB_txn* txn);

    private:
    void AddGlobalStake(RepEpochInfo const & info, MDB_txn* txn);

    void AddGlobalTotalReward(
            uint32_t const & epoch,
            Amount const & to_add,
            MDB_txn* txn);

    void SubtractGlobalRemainingReward(
            uint32_t const & epoch,
            Amount const & to_subtract,
            MDB_txn* txn);

    private:
    BlockStore & _store;
    Log          _log;

};
