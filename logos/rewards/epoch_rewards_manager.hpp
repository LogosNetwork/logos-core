#pragma once
#include <logos/common.hpp>
#include <logos/blockstore.hpp>
#include <logos/rewards/epoch_rewards.hpp>


//TODO include rep address in this struct?
struct RepEpochInfo
{
    RepEpochInfo(uint8_t levy,
                 uint32_t epoch,
                 Amount total_stake,
                 Amount self_stake)
        : levy_percentage(levy)
        , epoch_number(epoch)
        , total_stake(total_stake)
        , self_stake(self_stake)
    {}

    uint8_t  levy_percentage;
    uint32_t epoch_number;
    Amount   total_stake;
    Amount   self_stake;
};

const size_t EPOCH_REWARDS_KEYSIZE = 36;

class EpochRewardsManager
{
    using BlockStore = logos::block_store;
    static std::shared_ptr<EpochRewardsManager> instance;

public:

    using Key = std::array<uint8_t, EPOCH_REWARDS_KEYSIZE>;

    static void SetInstance(BlockStore& store)
    {
       instance.reset(new EpochRewardsManager(store)); 
    }

    static std::shared_ptr<EpochRewardsManager> GetInstance()
    {
        return instance;
    }

    EpochRewardsManager(BlockStore & store);

    void Init(AccountAddress const & rep_address,
              RepEpochInfo const & rep_epoch_info,
              MDB_txn * txn);

    bool SetTotalReward(AccountAddress const & rep_address,
                        uint32_t const & epoch_number,
                        Amount const & total_reward,
                        MDB_txn* txn);

    bool SetTotalGlobalReward(
        uint32_t const & epoch_number,
        Amount const & total_reward,
        MDB_txn* txn);

    bool HarvestReward(
            AccountAddress const & rep_address,
            uint32_t const & epoch_number,
            Amount const & harvest_amount,
            EpochRewardsInfo & info,
            MDB_txn* txn);

    void HarvestGlobalReward(
        uint32_t const & epoch,
        Amount const & to_subtract,
        GlobalEpochRewardsInfo global_info,
        MDB_txn* txn);

    EpochRewardsInfo GetEpochRewardsInfo(
            AccountAddress const & rep_address,
            uint32_t const & epoch_number,
            MDB_txn* txn);

    void RemoveEpochRewardsInfo(
        AccountAddress const & rep_address,
        uint32_t const & epoch_number,
        MDB_txn* txn);

    EpochRewardsInfo GetEpochRewardsInfo(Key & key,
                                         MDB_txn * txn);

    Key MakeKey(AccountAddress const & account,
                uint32_t const & epoch);

    GlobalEpochRewardsInfo GetGlobalEpochRewardsInfo(uint32_t const & epoch_number,
                                                     MDB_txn* txn);

    void RemoveGlobalRewards(uint32_t const & epoch_number, MDB_txn * txn);

    // TODO: RewardsAvailable
    bool HasRewards(AccountAddress const & rep_address,
                    uint32_t const & epoch_number,
                    MDB_txn* txn);

    bool GlobalRewardsAvailable(uint32_t const & epoch_number,
                                MDB_txn* txn);

private:

    //Need to keep track of the total stake that voted in an epoch
    //because rewards are distributed based on a reps percentage
    //of total stake that voted (sum of all voting reps stakes)
    void AddGlobalStake(RepEpochInfo const & info, MDB_txn* txn);

    void AddGlobalTotalReward(
            uint32_t const & epoch,
            Amount const & to_add,
            MDB_txn* txn);

    BlockStore & _store;
    Log          _log;
};
