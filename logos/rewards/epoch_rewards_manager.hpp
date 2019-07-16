#pragma once

#include <logos/common.hpp>
#include <logos/blockstore.hpp>
#include <logos/rewards/epoch_rewards.hpp>

struct RepEpochInfo
{
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

    static void SetInstance(BlockStore & store)
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

    bool OnFeeCollected(uint32_t epoch_number,
                        const Amount & value,
                        MDB_txn * txn);

    bool GetFeePool(uint32_t epoch_number,
                    Amount & value,
                    MDB_txn * txn = nullptr);

    bool RemoveFeePool(uint32_t epoch_number,
                       MDB_txn * txn);

    bool SetGlobalReward(uint32_t const & epoch_number,
                         Amount const & total_reward,
                         MDB_txn * txn);

    bool HarvestReward(AccountAddress const & rep_address,
                       uint32_t const & epoch_number,
                       Amount const & harvest_amount,
                       RewardsInfo & info,
                       MDB_txn * txn);

    void HarvestGlobalReward(uint32_t const & epoch,
                             Amount const & to_subtract,
                             GlobalRewardsInfo global_info,
                             MDB_txn * txn);

    RewardsInfo GetRewardsInfo(AccountAddress const & rep_address,
                               uint32_t const & epoch_number,
                               MDB_txn *txn);

    GlobalRewardsInfo GetGlobalRewardsInfo(uint32_t const & epoch_number,
                                           MDB_txn *txn);
    
    bool RewardsAvailable(AccountAddress const & rep_address,
                          uint32_t const & epoch_number,
                          MDB_txn * txn);

    bool GlobalRewardsAvailable(uint32_t const & epoch_number,
                                MDB_txn * txn);

    Key MakeKey(AccountAddress const & account,
                uint32_t const & epoch);

private:

    RewardsInfo DoGetRewardsInfo(Key & key,
                                 MDB_txn * txn);

    // Need to keep track of the total stake that voted in an epoch
    // because rewards are distributed based on a reps percentage
    // of total stake that voted (sum of all voting reps stakes)
    void AddGlobalStake(RepEpochInfo const & info, MDB_txn * txn);

    BlockStore & _store;
    Log          _log;
};
