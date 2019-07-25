#include <gtest/gtest.h>
#include <logos/rewards/epoch_rewards_manager.hpp>
#include <logos/unit_test/msg_validator_setup.hpp>

#define Unit_Test_Epoch_Rewards_DB

#ifdef Unit_Test_Epoch_Rewards_DB


TEST(Epoch_Rewards_DB, RewardsManager)
{
    logos::block_store* store = get_db();
    logos::transaction txn(store->environment, nullptr, true);
    store->clear(store->rewards_db,txn);
    store->clear(store->global_rewards_db,txn);

    EpochRewardsManager rewards_mgr(*store);


    AccountAddress rep = 12345;
    uint32_t epoch_num = 42;
    uint8_t levy = 15;
    Amount stake = 30000;
    RepEpochInfo info{levy,epoch_num,stake,stake};

    rewards_mgr.Init(rep, info, txn);

    RewardsInfo rewards_info
        = rewards_mgr.GetRewardsInfo(rep, epoch_num, txn);

    GlobalRewardsInfo global_info
        = rewards_mgr.GetGlobalRewardsInfo(epoch_num, txn);
    
    ASSERT_EQ(rewards_info.levy_percentage, levy);
    ASSERT_EQ(rewards_info.total_stake, stake);
    ASSERT_EQ(rewards_info.remaining_reward,0);
    ASSERT_EQ(rewards_info.total_reward,0);
    ASSERT_EQ(global_info.total_stake, stake);
    ASSERT_EQ(global_info.remaining_reward,0);
    ASSERT_EQ(global_info.total_reward,0);

    Rational total_reward = 100000;

    rewards_info.total_reward = total_reward;
    rewards_info.remaining_reward = total_reward;

    rewards_mgr.HarvestReward(rep,epoch_num,0,rewards_info,txn);
    rewards_mgr.SetGlobalReward(epoch_num, total_reward.numerator().convert_to<logos::uint128_t>(), txn);

    rewards_info = rewards_mgr.GetRewardsInfo(rep, epoch_num, txn);
    global_info = rewards_mgr.GetGlobalRewardsInfo(epoch_num, txn);
    ASSERT_EQ(rewards_info.total_reward, total_reward);
    ASSERT_EQ(rewards_info.remaining_reward, total_reward);
    ASSERT_EQ(global_info.total_reward.number(), total_reward);
    ASSERT_EQ(global_info.remaining_reward, total_reward);

    Rational harvest_amount = 1000;

    rewards_mgr.HarvestGlobalReward(epoch_num,harvest_amount,global_info,txn);
    ASSERT_FALSE(rewards_mgr.HarvestReward(rep,epoch_num,harvest_amount,rewards_info,txn));

    rewards_info = rewards_mgr.GetRewardsInfo(rep, epoch_num, txn);
    global_info = rewards_mgr.GetGlobalRewardsInfo(epoch_num, txn);
    ASSERT_EQ(rewards_info.total_reward, total_reward);
    ASSERT_EQ(rewards_info.remaining_reward, total_reward-harvest_amount);
    ASSERT_EQ(global_info.total_reward.number(), total_reward);
    ASSERT_EQ(global_info.remaining_reward, total_reward-harvest_amount);

    rewards_mgr.HarvestGlobalReward(epoch_num,harvest_amount,global_info,txn);
    ASSERT_FALSE(rewards_mgr.HarvestReward(rep,epoch_num,harvest_amount,rewards_info,txn));


    rewards_info = rewards_mgr.GetRewardsInfo(rep, epoch_num, txn);
    global_info = rewards_mgr.GetGlobalRewardsInfo(epoch_num, txn);
    ASSERT_EQ(rewards_info.total_reward, total_reward);
    ASSERT_EQ(rewards_info.remaining_reward, total_reward-(harvest_amount+harvest_amount));
    ASSERT_EQ(global_info.total_reward.number(), total_reward);
    ASSERT_EQ(global_info.remaining_reward, total_reward-(harvest_amount+harvest_amount));

    rewards_mgr.HarvestGlobalReward(epoch_num,rewards_info.remaining_reward,global_info,txn);
    ASSERT_FALSE(rewards_mgr.HarvestReward(rep,epoch_num,rewards_info.remaining_reward,rewards_info,txn));

    logos::mdb_val val;
    auto key = rewards_mgr.MakeKey(rep,epoch_num);
    
    ASSERT_EQ(
            mdb_get(
                txn,
                store->rewards_db,
                logos::mdb_val(key.size(),key.data()),
                val),
            MDB_NOTFOUND); 

    ASSERT_EQ(
            mdb_get(
                txn,
                store->global_rewards_db,
                logos::mdb_val(sizeof(epoch_num),const_cast<uint32_t *>(&epoch_num)),
                val),
            MDB_NOTFOUND); 


    


    std::vector<std::pair<AccountAddress,RepEpochInfo>> reps;
    std::unordered_map<uint32_t,Amount> global_total_stakes;

    for(uint32_t e = 25; e < 50; e++)
    {
        Amount global_total = 0;
        for(size_t r = 0; r < 100; r++)
        {
            uint8_t levy = e % 2 == 0 ? 25 : 10;
            Amount stake = e % 3 == 0 ? 40000 : e % 3 == 1 ? 10000 : 2500;
            levy *= ((r % 3) + 1);
            stake = stake.number() * ((r % 3) + 1);
            RepEpochInfo info{levy,e,stake,stake};
            reps.push_back(std::make_pair(r,info));
            global_total += stake;
        }
        global_total_stakes[e] = global_total;
    }

    for(auto rep : reps)
    {
        rewards_mgr.Init(rep.first,rep.second,txn);
    }

    for(auto rep : reps)
    {
        RewardsInfo rewards_info
            = rewards_mgr.GetRewardsInfo(rep.first, rep.second.epoch_number, txn);

        ASSERT_EQ(rewards_info.levy_percentage, rep.second.levy_percentage);
        ASSERT_EQ(rewards_info.total_stake, rep.second.total_stake);
        ASSERT_EQ(rewards_info.remaining_reward,0);
        ASSERT_EQ(rewards_info.total_reward,0);
    }

    for(size_t e = 25; e < 50; ++e)
    {
        
        GlobalRewardsInfo global_info
            = rewards_mgr.GetGlobalRewardsInfo(e, txn);
        ASSERT_EQ(global_info.total_stake,global_total_stakes[e]);
    }
}

#endif
