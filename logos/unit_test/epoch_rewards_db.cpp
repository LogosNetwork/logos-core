#include <gtest/gtest.h>
#include <logos/rewards/epoch_rewards_manager.hpp>
#include <logos/unit_test/msg_validator_setup.hpp>

#define Unit_Test_Epoch_Rewards_DB

#ifdef Unit_Test_Epoch_Rewards_DB


TEST(Epoch_Rewards_DB, RewardsManager)
{

    logos::block_store* store = get_db();

    logos::transaction txn(store->environment, nullptr, true);

    EpochRewardsManager rewards_mgr(*store);


    AccountAddress rep = 12345;
    uint32_t epoch_num = 42;
    uint8_t levy = 15;
    Amount stake = 30000;
    RepEpochInfo info{levy,epoch_num,stake};

    rewards_mgr.Init(rep, info, txn);

    EpochRewardsInfo rewards_info 
        = rewards_mgr.GetEpochRewardsInfo(rep,epoch_num,txn);
    
    ASSERT_EQ(rewards_info.levy_percentage, levy);
    ASSERT_EQ(rewards_info.total_stake, stake);
    ASSERT_EQ(rewards_info.remaining_reward,0);
    ASSERT_EQ(rewards_info.total_reward,0);

    Amount total_reward = 100000;

    rewards_mgr.SetTotalReward(rep,epoch_num,total_reward,txn);

    rewards_info = rewards_mgr.GetEpochRewardsInfo(rep,epoch_num,txn);
    ASSERT_EQ(rewards_info.total_reward, total_reward);
    ASSERT_EQ(rewards_info.remaining_reward, total_reward);

    Amount harvest_amount = 1000;

    ASSERT_FALSE(rewards_mgr.HarvestReward(rep,epoch_num,harvest_amount,txn));

    rewards_info = rewards_mgr.GetEpochRewardsInfo(rep,epoch_num,txn);
    ASSERT_EQ(rewards_info.total_reward, total_reward);
    ASSERT_EQ(rewards_info.remaining_reward, total_reward-harvest_amount);

    ASSERT_FALSE(rewards_mgr.HarvestReward(rep,epoch_num,harvest_amount,txn));


    rewards_info = rewards_mgr.GetEpochRewardsInfo(rep,epoch_num,txn);
    ASSERT_EQ(rewards_info.total_reward, total_reward);
    ASSERT_EQ(rewards_info.remaining_reward, total_reward-(harvest_amount+harvest_amount));

    ASSERT_FALSE(rewards_mgr.HarvestReward(rep,epoch_num,rewards_info.remaining_reward,txn));

    logos::mdb_val val;
    auto key = rewards_mgr.MakeKey(rep,epoch_num);
    
    ASSERT_EQ(
            mdb_get(
                txn,
                store->epoch_rewards_db,
                logos::mdb_val(key.size(),key.data()),
                val),
            MDB_NOTFOUND); 

    


    std::vector<std::pair<AccountAddress,RepEpochInfo>> reps;

    for(uint32_t e = 25; e < 50; e++)
    {
        for(size_t r = 0; r < 100; r++)
        {
            uint8_t levy = e % 2 == 0 ? 25 : 10;
            Amount stake = e % 3 == 0 ? 40000 : e % 3 == 1 ? 10000 : 2500;
            levy *= ((r % 3) + 1);
            stake = stake.number() * ((r % 3) + 1);
            RepEpochInfo info{levy,e,stake};
            reps.push_back(std::make_pair(r,info));
        }
    }

    for(auto rep : reps)
    {
        rewards_mgr.Init(rep.first,rep.second,txn);
    }

    for(auto rep : reps)
    {
        EpochRewardsInfo rewards_info
            = rewards_mgr.GetEpochRewardsInfo(rep.first,rep.second.epoch_number,txn);

        ASSERT_EQ(rewards_info.levy_percentage, rep.second.levy_percentage);
        ASSERT_EQ(rewards_info.total_stake, rep.second.total_stake);
        ASSERT_EQ(rewards_info.remaining_reward,0);
        ASSERT_EQ(rewards_info.total_reward,0);
    }


    

}

#endif
