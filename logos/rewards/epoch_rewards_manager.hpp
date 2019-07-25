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

    void Init(const AccountAddress & rep_address,
              const RepEpochInfo & rep_epoch_info,
              MDB_txn * txn);

    bool OnFeeCollected(const uint32_t epoch_number,
                        const Amount & value,
                        MDB_txn * txn);

    bool GetFeePool(const uint32_t epoch_number,
                    Amount & value,
                    MDB_txn * txn = nullptr);

    bool RemoveFeePool(const uint32_t epoch_number,
                       MDB_txn * txn);

    bool SetGlobalReward(const uint32_t epoch_number,
                         const Amount & total_reward,
                         MDB_txn * txn);

    bool HarvestReward(const AccountAddress & rep_address,
                       const uint32_t epoch_number,
                       const Rational & harvest_amount,
                       RewardsInfo & info,
                       MDB_txn * txn);

    void HarvestGlobalReward(const uint32_t epoch_number,
                             const Rational & harvest_amount,
                             GlobalRewardsInfo global_info,
                             MDB_txn * txn);

    RewardsInfo GetRewardsInfo(AccountAddress const & rep_address,
                               const uint32_t epoch_number,
                               MDB_txn * txn);

    GlobalRewardsInfo GetGlobalRewardsInfo(const uint32_t epoch_number,
                                           MDB_txn * txn);
    
    bool RewardsAvailable(const AccountAddress & rep_address,
                          const uint32_t epoch_number,
                          MDB_txn * txn);

    bool GlobalRewardsAvailable(const uint32_t epoch_number,
                                MDB_txn * txn);

    Key MakeKey(const AccountAddress & account,
                const uint32_t epoch_number);

private:

    RewardsInfo DoGetRewardsInfo(Key & key,
                                 MDB_txn * txn);

    // Need to keep track of the total stake that voted in an epoch
    // because rewards are distributed based on a reps percentage
    // of total stake that voted (sum of all voting reps stakes)
    void AddGlobalStake(const RepEpochInfo & info, MDB_txn * txn);

    BlockStore & _store;
    Log          _log;
};
