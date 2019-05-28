#include <gtest/gtest.h>
#include <logos/unit_test/msg_validator_setup.hpp>
#include <logos/staking/staking_manager.hpp>
#include <logos/staking/voting_power_manager.hpp>

#define Unit_Test_Staking_Manager

#ifdef Unit_Test_Staking_Manager

TEST(Staking_Manager, Stake)
{
    logos::block_store* store = get_db(); 
    clear_dbs();
    logos::transaction txn(store->environment, nullptr, true);

    StakingManager staking_mgr(*store);
    VotingPowerManager voting_power_mgr(*store);
    LiabilityManager liability_mgr(*store);

    uint32_t epoch = 100;

    logos::account_info info;
    auto set_rep = [&](AccountAddress const & rep)
    {
        return;
        Proxy req;
        req.rep = rep;
        req.Hash();
        store->request_put(req,txn);
        info.staking_subchain_head = req.Hash();
        info.rep = rep;
    };


    AccountAddress target = 84;
    voting_power_mgr.AddSelfStake(target, 10, epoch, txn);
    Amount initial_balance = 1000;
    info.SetBalance(initial_balance, epoch, txn);
    AccountAddress origin = 42;
    set_rep(target);

    VotingPowerInfo vp_info;
    voting_power_mgr.GetVotingPowerInfo(target, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, 0);



    store->account_put(origin, info, txn);
    std::vector<ThawingFunds> all_thawing(staking_mgr.GetThawingFunds(origin, txn));

    ASSERT_EQ(all_thawing.size(), 0);

    Amount to_stake = 50;
    auto get_secondary_liabilities = [&]()
    {
        std::vector<Liability> secondary;
        auto hashes = liability_mgr.GetSecondaryLiabilities(origin,txn);
        for(auto h : hashes)
        {
            secondary.push_back(liability_mgr.Get(h,txn));
        }
        return secondary;
    };

    //Stake to a rep
    staking_mgr.Stake(origin, info, to_stake, target, epoch, txn);

    StakedFunds cur_stake;
    staking_mgr.GetCurrentStakedFunds(origin, cur_stake, txn);
    all_thawing = staking_mgr.GetThawingFunds(origin, txn);
    ASSERT_EQ(all_thawing.size(), 0);
    ASSERT_EQ(cur_stake.amount, to_stake);
    ASSERT_EQ(cur_stake.target, target);
    ASSERT_EQ(info.GetAvailableBalance(),initial_balance - to_stake);
    ASSERT_EQ(info.GetBalance(),initial_balance);

    voting_power_mgr.GetVotingPowerInfo(target, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, to_stake);
    ASSERT_EQ(vp_info.next.unlocked_proxied,info.GetAvailableBalance());

    //Increase stake
    to_stake += 100;
    staking_mgr.Stake(origin, info, to_stake, target, epoch, txn);

    staking_mgr.GetCurrentStakedFunds(origin, cur_stake, txn);
    all_thawing = staking_mgr.GetThawingFunds(origin, txn);
    ASSERT_EQ(all_thawing.size(), 0);
    ASSERT_EQ(cur_stake.amount, to_stake);
    ASSERT_EQ(cur_stake.target, target);
    ASSERT_EQ(info.GetAvailableBalance(),initial_balance - to_stake);
    ASSERT_EQ(info.GetBalance(),initial_balance);

    voting_power_mgr.GetVotingPowerInfo(target, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, to_stake);
    ASSERT_EQ(vp_info.next.unlocked_proxied,info.GetAvailableBalance());


    //Decrease stake
    to_stake -= 50;
    staking_mgr.Stake(origin, info, to_stake, target, epoch, txn);

    all_thawing = staking_mgr.GetThawingFunds(origin, txn);
    ASSERT_EQ(all_thawing.size(), 1);
    ASSERT_EQ(all_thawing[0].target, target);
    ASSERT_EQ(all_thawing[0].amount, 50);
    ASSERT_EQ(all_thawing[0].expiration_epoch,epoch + 42);
    staking_mgr.GetCurrentStakedFunds(origin, cur_stake, txn);
    ASSERT_EQ(cur_stake.amount, to_stake);
    ASSERT_EQ(cur_stake.target, target);
    ASSERT_EQ(info.GetAvailableBalance(),initial_balance - to_stake - 50);
    ASSERT_EQ(info.GetBalance(),initial_balance);

    voting_power_mgr.GetVotingPowerInfo(target, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, to_stake);
    ASSERT_EQ(vp_info.next.unlocked_proxied,info.GetAvailableBalance());

    //Decrease stake again
    to_stake -= 25;
    staking_mgr.Stake(origin, info, to_stake, target, epoch, txn);
    all_thawing = staking_mgr.GetThawingFunds(origin, txn);
    ASSERT_EQ(all_thawing.size(), 1);
    ASSERT_EQ(all_thawing[0].target, target);
    ASSERT_EQ(all_thawing[0].amount, 75);
    ASSERT_EQ(all_thawing[0].expiration_epoch,epoch + 42);
    staking_mgr.GetCurrentStakedFunds(origin, cur_stake, txn);
    ASSERT_EQ(cur_stake.amount, to_stake);
    ASSERT_EQ(cur_stake.target, target);
    ASSERT_EQ(info.GetAvailableBalance(),initial_balance - to_stake - 50 - 25);
    ASSERT_EQ(info.GetBalance(),initial_balance);

    voting_power_mgr.GetVotingPowerInfo(target, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, to_stake);
    ASSERT_EQ(vp_info.next.unlocked_proxied,info.GetAvailableBalance());

    //Change target (creates some thawing)
    AccountAddress target2 = 85;
    set_rep(target2);
    voting_power_mgr.AddSelfStake(target2,10,epoch,txn);
    to_stake -= 20;

    staking_mgr.Stake(origin, info, to_stake, target2, epoch, txn);
    all_thawing = staking_mgr.GetThawingFunds(origin, txn);
    ASSERT_EQ(all_thawing.size(), 1);
    ASSERT_EQ(all_thawing[0].target, 84);
    ASSERT_EQ(all_thawing[0].amount, 95);
    ASSERT_EQ(all_thawing[0].expiration_epoch,epoch + 42);
    staking_mgr.GetCurrentStakedFunds(origin, cur_stake, txn);
    ASSERT_EQ(cur_stake.amount, to_stake);
    ASSERT_EQ(cur_stake.target, target2);
    ASSERT_EQ(info.GetAvailableBalance(),initial_balance - to_stake - 50 - 25 - 20);
    ASSERT_EQ(info.GetBalance(),initial_balance);

    voting_power_mgr.GetVotingPowerInfo(target2, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, to_stake);
    ASSERT_EQ(vp_info.current.locked_proxied, 0);
    ASSERT_EQ(vp_info.current.unlocked_proxied, 0);
    ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance());
    voting_power_mgr.GetVotingPowerInfo(target, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, 0);
    ASSERT_EQ(vp_info.next.unlocked_proxied, 0);

    std::vector<Liability> secondary(get_secondary_liabilities());
    ASSERT_EQ(secondary.size(),1);
    ASSERT_EQ(secondary[0].target,target);
    ASSERT_EQ(secondary[0].amount,to_stake);
    ASSERT_EQ(secondary[0].expiration_epoch,epoch+42);
    ASSERT_EQ(secondary[0].source,origin);


    //Increase stake to new target (uses thawing)
    to_stake += 50;
    staking_mgr.Stake(origin, info, to_stake, target2, epoch, txn);
    all_thawing = staking_mgr.GetThawingFunds(origin, txn);
    ASSERT_EQ(all_thawing.size(), 1);
    ASSERT_EQ(all_thawing[0].target, 84);
    ASSERT_EQ(all_thawing[0].amount, 45);
    ASSERT_EQ(all_thawing[0].expiration_epoch,epoch + 42);
    staking_mgr.GetCurrentStakedFunds(origin, cur_stake, txn);
    ASSERT_EQ(cur_stake.amount, to_stake);
    ASSERT_EQ(cur_stake.target, target2);
    ASSERT_EQ(info.GetAvailableBalance(),initial_balance - to_stake - 45);
    ASSERT_EQ(info.GetBalance(),initial_balance);

    voting_power_mgr.GetVotingPowerInfo(target2, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, to_stake);
    voting_power_mgr.GetVotingPowerInfo(target, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, 0);
    ASSERT_EQ(vp_info.next.unlocked_proxied, 0);

    secondary = get_secondary_liabilities();
    ASSERT_EQ(secondary.size(),1);
    ASSERT_EQ(secondary[0].target,target);
    ASSERT_EQ(secondary[0].amount,to_stake);
    ASSERT_EQ(secondary[0].expiration_epoch,epoch+42);
    ASSERT_EQ(secondary[0].source,origin);



    //Stake the rest of thawing funds, and then some available funds
    to_stake += 65;

    staking_mgr.Stake(origin, info, to_stake, target2, epoch, txn);
    all_thawing = staking_mgr.GetThawingFunds(origin, txn);
    ASSERT_EQ(all_thawing.size(), 0);
    staking_mgr.GetCurrentStakedFunds(origin, cur_stake, txn);
    ASSERT_EQ(cur_stake.amount, to_stake);
    ASSERT_EQ(cur_stake.target, target2);
    ASSERT_EQ(info.GetAvailableBalance(),initial_balance - to_stake);
    ASSERT_EQ(info.GetBalance(),initial_balance);

    voting_power_mgr.GetVotingPowerInfo(target2, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, to_stake);
    voting_power_mgr.GetVotingPowerInfo(target, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, 0);
    ASSERT_EQ(vp_info.next.unlocked_proxied, 0);

    secondary = get_secondary_liabilities();
    ASSERT_EQ(secondary.size(),1);
    ASSERT_EQ(secondary[0].target,target);
    ASSERT_EQ(secondary[0].amount,to_stake-20);
    ASSERT_EQ(secondary[0].expiration_epoch,epoch+42);
    ASSERT_EQ(secondary[0].source,origin);


    //Create thawing to new target
    to_stake -= 50;
    staking_mgr.Stake(origin, info, to_stake, target2, epoch, txn);
    all_thawing = staking_mgr.GetThawingFunds(origin, txn);
    ASSERT_EQ(all_thawing.size(), 1);
    ASSERT_EQ(all_thawing[0].target, target2);
    ASSERT_EQ(all_thawing[0].amount, 50);
    ASSERT_EQ(all_thawing[0].expiration_epoch,epoch + 42);

    //Change target again (uses available funds)
    AccountAddress target3 = 5001;
    set_rep(target3);
    voting_power_mgr.AddSelfStake(target3,10,epoch,txn);
    to_stake += 100;
    staking_mgr.Stake(origin, info, to_stake, target3, epoch, txn);
    all_thawing = staking_mgr.GetThawingFunds(origin, txn);
    ASSERT_EQ(all_thawing.size(), 1);
    ASSERT_EQ(info.GetAvailableBalance(),initial_balance - to_stake - to_stake + 100 - 50);

    secondary = get_secondary_liabilities();
    ASSERT_EQ(secondary.size(),1);
    ASSERT_EQ(secondary[0].target,target);
    ASSERT_EQ(secondary[0].amount,to_stake-20+50-100);
    ASSERT_EQ(secondary[0].expiration_epoch,epoch+42);
    ASSERT_EQ(secondary[0].source,origin);



}

TEST(Staking_Manager, StakeEpochTransition)
{

    logos::block_store* store = get_db(); 
    clear_dbs();
    logos::transaction txn(store->environment, nullptr, true);

    StakingManager staking_mgr(*store);
    VotingPowerManager voting_power_mgr(*store);

    uint32_t epoch = 100;

    logos::account_info info;
    auto set_rep = [&](AccountAddress const & rep)
    {
        return;
        Proxy req;
        req.rep = rep;
        req.Hash();
        store->request_put(req,txn);
        info.staking_subchain_head = req.Hash();
        info.rep = rep;
    };
    AccountAddress target = 84;
    voting_power_mgr.AddSelfStake(target, 10, epoch, txn);
    Amount initial_balance = 1000;
    info.SetBalance(initial_balance, epoch, txn);
    AccountAddress origin = 42;
    set_rep(target);

    VotingPowerInfo vp_info;
    voting_power_mgr.GetVotingPowerInfo(target, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, 0);
    ASSERT_EQ(vp_info.next.unlocked_proxied, 0);



    store->account_put(origin, info, txn);
    std::vector<ThawingFunds> all_thawing(staking_mgr.GetThawingFunds(origin, txn));

    ASSERT_EQ(all_thawing.size(), 0);

    Amount to_stake = 100;
    Amount cur_thawing = 0;
    //initial stake
    staking_mgr.Stake(origin, info, to_stake, target, epoch, txn);
    ASSERT_EQ(info.GetAvailableBalance(),initial_balance-to_stake);
    
    voting_power_mgr.GetVotingPowerInfo(target, epoch, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, to_stake);
    ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance());
    ASSERT_EQ(vp_info.current.locked_proxied, 0);
    ASSERT_EQ(vp_info.current.unlocked_proxied, 0);


    ++epoch;

    ASSERT_EQ(info.GetAvailableBalance(),initial_balance-to_stake);
    voting_power_mgr.GetVotingPowerInfo(target, epoch, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, to_stake);
    ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance());
    ASSERT_EQ(vp_info.current.locked_proxied, to_stake);
    ASSERT_EQ(vp_info.current.unlocked_proxied, info.GetAvailableBalance());

    //increase stake
    to_stake += 50;

    staking_mgr.Stake(origin, info, to_stake, target, epoch, txn);

    ASSERT_EQ(info.GetAvailableBalance(),initial_balance-to_stake);
    voting_power_mgr.GetVotingPowerInfo(target, epoch, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, to_stake);
    ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance());
    ASSERT_EQ(vp_info.current.locked_proxied, to_stake - 50);
    ASSERT_EQ(vp_info.current.unlocked_proxied, info.GetAvailableBalance()+50);


    ++epoch;

    ASSERT_EQ(info.GetAvailableBalance(),initial_balance-to_stake);
    voting_power_mgr.GetVotingPowerInfo(target, epoch, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, to_stake);
    ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance());
    ASSERT_EQ(vp_info.current.locked_proxied, to_stake);
    ASSERT_EQ(vp_info.current.unlocked_proxied, info.GetAvailableBalance());



    to_stake -= 75;
    cur_thawing += 75;
    //decrease stake
    staking_mgr.Stake(origin, info, to_stake, target, epoch, txn);

    ASSERT_EQ(info.GetAvailableBalance(),initial_balance-to_stake-cur_thawing);
    voting_power_mgr.GetVotingPowerInfo(target, epoch, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, to_stake);
    ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance());
    ASSERT_EQ(vp_info.current.locked_proxied, to_stake + cur_thawing);
    ASSERT_EQ(vp_info.current.unlocked_proxied, info.GetAvailableBalance());

    ++epoch;
    ASSERT_EQ(info.GetAvailableBalance(),initial_balance-to_stake-cur_thawing);
    voting_power_mgr.GetVotingPowerInfo(target, epoch, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, to_stake);
    ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance());
    ASSERT_EQ(vp_info.current.locked_proxied, to_stake);
    ASSERT_EQ(vp_info.current.unlocked_proxied, info.GetAvailableBalance());

    //change target
    AccountAddress target2 = 4567;
    set_rep(target2);
    voting_power_mgr.AddSelfStake(target2,10,epoch,txn); 

    to_stake -= 25;
    cur_thawing += 25;
    staking_mgr.Stake(origin, info, to_stake, target2, epoch, txn);

    ASSERT_EQ(info.GetAvailableBalance(),initial_balance-to_stake-cur_thawing);
    voting_power_mgr.GetVotingPowerInfo(target, epoch, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, 0);
    ASSERT_EQ(vp_info.next.unlocked_proxied, 0);
    ASSERT_EQ(vp_info.current.locked_proxied, to_stake + 25);
    ASSERT_EQ(vp_info.current.unlocked_proxied, info.GetAvailableBalance());
    voting_power_mgr.GetVotingPowerInfo(target2, epoch, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, to_stake);
    ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance());
    ASSERT_EQ(vp_info.current.locked_proxied, 0);
    ASSERT_EQ(vp_info.current.unlocked_proxied, 0);    
}


TEST(Staking_Manager, Validate)
{
    logos::block_store* store = get_db(); 
    clear_dbs();
    logos::transaction txn(store->environment, nullptr, true);

    StakingManager staking_mgr(*store);
    VotingPowerManager voting_power_mgr(*store);
    LiabilityManager liability_mgr(*store);

    uint32_t epoch = 100;

    logos::account_info info;
    auto set_rep = [&](AccountAddress const & rep)
    {
        return;
        Proxy req;
        req.rep = rep;
        req.Hash();
        store->request_put(req,txn);
        info.staking_subchain_head = req.Hash();
        info.rep = rep;
    };


    AccountAddress target = 8020;
    set_rep(target);
    voting_power_mgr.AddSelfStake(target, 10, epoch, txn);
    Amount initial_balance = 1000;
    info.SetBalance(initial_balance, epoch, txn);
    AccountAddress origin = 42;

    VotingPowerInfo vp_info;
    voting_power_mgr.GetVotingPowerInfo(target, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, 0);



    store->account_put(origin, info, txn);
    std::vector<ThawingFunds> all_thawing(staking_mgr.GetThawingFunds(origin, txn));

    ASSERT_EQ(all_thawing.size(), 0);

    Amount to_stake = 50;
    auto get_secondary_liabilities = [&]()
    {
        std::vector<Liability> secondary;
        auto hashes = liability_mgr.GetSecondaryLiabilities(origin,txn);
        for(auto h : hashes)
        {
            secondary.push_back(liability_mgr.Get(h,txn));
        }
        return secondary;
    };

    //basic validation
    ASSERT_TRUE(staking_mgr.Validate(origin, info, to_stake, target, epoch, 0, txn));
    ASSERT_FALSE(staking_mgr.Validate(origin, info, initial_balance+10, target, epoch, 0, txn));

    staking_mgr.Stake(origin, info, to_stake, target, epoch, txn);
    ASSERT_EQ(info.GetAvailableBalance(),initial_balance-to_stake);
    ASSERT_TRUE(initial_balance-to_stake+10 > info.GetAvailableBalance());

    ASSERT_TRUE(staking_mgr.Validate(origin, info, to_stake, target, epoch, 0, txn));
    ASSERT_TRUE(staking_mgr.Validate(origin, info, initial_balance, target, epoch, 0, txn));
    ASSERT_FALSE(staking_mgr.Validate(origin, info, initial_balance+10, target, epoch, 0, txn));


    //able to stake thawing
    to_stake = 0;
    ASSERT_TRUE(staking_mgr.Validate(origin, info, 0, target, epoch, 0, txn));
    staking_mgr.Stake(origin, info, to_stake, target, epoch, txn);
    ASSERT_TRUE(staking_mgr.Validate(origin, info, initial_balance, target, epoch, 0, txn));
    ASSERT_FALSE(staking_mgr.Validate(origin, info, initial_balance+10, target, epoch, 0, txn));
    

    //able to change target, with existing thawing and staked funds
    AccountAddress target2 = 45333;
    ASSERT_TRUE(staking_mgr.Validate(origin, info, initial_balance, target2, epoch, 0, txn));
    to_stake = 20;

    staking_mgr.Stake(origin, info, to_stake, target, epoch, txn);
    ASSERT_TRUE(staking_mgr.Validate(origin, info, initial_balance, target2, epoch, 0, txn));
    ASSERT_TRUE(staking_mgr.Validate(origin, info, to_stake-10, target2, epoch, 0, txn));
    ASSERT_TRUE(staking_mgr.Validate(origin, info, to_stake, target2, epoch, 0, txn));

    staking_mgr.Stake(origin, info, 0, target, epoch, txn);
    ASSERT_TRUE(staking_mgr.Validate(origin, info, initial_balance, target2, epoch, 0, txn));
    ASSERT_TRUE(staking_mgr.Validate(origin, info, 0, target2, epoch, 0, txn));
    ASSERT_FALSE(staking_mgr.Validate(origin, info, initial_balance+1, target2, epoch, 0, txn));
    to_stake = 100;
    set_rep(target2);
    voting_power_mgr.AddSelfStake(target2, 10, epoch, txn);
    staking_mgr.Stake(origin, info, to_stake, target2, epoch, txn);
    
    ASSERT_TRUE(staking_mgr.Validate(origin, info, initial_balance, target2, epoch, 0, txn));

    ASSERT_FALSE(staking_mgr.Validate(origin, info, initial_balance, target, epoch, 0, txn));
    AccountAddress target3 = 30000;
    ASSERT_FALSE(staking_mgr.Validate(origin, info, initial_balance, target3, epoch, 0, txn));
    ASSERT_TRUE(staking_mgr.Validate(origin,  info, initial_balance-to_stake, target, epoch, 0,txn));

    ASSERT_TRUE(staking_mgr.Validate(origin,  info, initial_balance-to_stake, target3, epoch, 0,txn));

    epoch += 42;

    ASSERT_TRUE(staking_mgr.Validate(origin, info, initial_balance, target, epoch, 0, txn));
    ASSERT_TRUE(staking_mgr.Validate(origin, info, initial_balance, target3, epoch, 0, txn));

    to_stake = 0;
    voting_power_mgr.AddSelfStake(target3, 10, epoch, txn);
    set_rep(target3);
    staking_mgr.Stake(origin, info, to_stake, target3, epoch, txn);
    to_stake = 50;
    ASSERT_TRUE(staking_mgr.Validate(origin, info, to_stake, target3, epoch, 0, txn));

}

TEST(Staking_Manager, Thawing)
{
    logos::block_store* store = get_db(); 
    clear_dbs();
    logos::transaction txn(store->environment, nullptr, true);

    StakingManager staking_mgr(*store);
    LiabilityManager liability_mgr(*store);

    AccountAddress origin = 456;
    logos::account_info info;
    Amount starting_balance = 100000;
    Amount starting_available = 100;
    info.SetBalance(starting_balance,0,txn);
    info.SetAvailableBalance(starting_available,0,txn);
    ASSERT_EQ(info.epoch_thawing_updated,0);
    AccountAddress target = 44;
    uint32_t epoch = 60;
    ThawingFunds t = staking_mgr.CreateThawingFunds(target,origin,epoch,txn);
    staking_mgr.Store(t, origin, txn);
    ++epoch;
    ThawingFunds t2 = staking_mgr.CreateThawingFunds(target,origin,epoch,txn);
    staking_mgr.Store(t2, origin, txn);

    ThawingFunds t3 = staking_mgr.CreateThawingFunds(target,origin,epoch-2,txn);
    staking_mgr.Store(t3, origin, txn);

    std::vector<ThawingFunds> thawing;
    staking_mgr.ProcessThawingFunds(origin,[&](ThawingFunds & funds) {
            thawing.push_back(funds);
            return true;
            }, txn);
    ASSERT_EQ(thawing.size(),3);
    //Test order
    ASSERT_EQ(thawing[2].expiration_epoch,epoch+42-2);
    ASSERT_EQ(thawing[1].expiration_epoch,epoch+42-1);
    ASSERT_EQ(thawing[0].expiration_epoch,epoch+42);

    //Consolidation
    ThawingFunds t4 = staking_mgr.CreateThawingFunds(target,origin,epoch-1,txn);
    t4.amount = 100;
    staking_mgr.Store(t4, origin, txn);

    thawing.clear();
    staking_mgr.ProcessThawingFunds(origin,[&](ThawingFunds & funds) {
            thawing.push_back(funds);
            return true;
            }, txn);
    ASSERT_EQ(thawing.size(),3);
    ASSERT_EQ(thawing[2].expiration_epoch,epoch+42-2);
    ASSERT_EQ(thawing[1].expiration_epoch,epoch+42-1);
    ASSERT_EQ(thawing[0].expiration_epoch,epoch+42);

    ASSERT_EQ(thawing[1].amount,t4.amount);
    ASSERT_EQ(thawing[0].amount,0);
    ASSERT_EQ(thawing[2].amount,0);

    ThawingFunds t5 = staking_mgr.CreateThawingFunds(target,origin,epoch-1,txn);
    t5.amount = 50;
    staking_mgr.Store(t5, origin, txn);

    thawing.clear();
    staking_mgr.ProcessThawingFunds(origin,[&](ThawingFunds & funds) {
            thawing.push_back(funds);
            return true;
            }, txn);
    ASSERT_EQ(thawing.size(),3);
    ASSERT_EQ(thawing[2].expiration_epoch,epoch+42-2);
    ASSERT_EQ(thawing[1].expiration_epoch,epoch+42-1);
    ASSERT_EQ(thawing[0].expiration_epoch,epoch+42);

    ASSERT_EQ(thawing[1].amount,t4.amount+t5.amount);
    ASSERT_EQ(thawing[0].amount,0);
    ASSERT_EQ(thawing[2].amount,0);

    ThawingFunds t6 = staking_mgr.CreateThawingFunds(target+1,origin,epoch-1,txn);
    t6.amount = 100;
    staking_mgr.Store(t6, origin, txn);
    thawing.clear();
    staking_mgr.ProcessThawingFunds(origin,[&](ThawingFunds & funds) {
            thawing.push_back(funds);
            return true;
            }, txn);
    ASSERT_EQ(thawing.size(),4);
    ASSERT_EQ(thawing[3].expiration_epoch,epoch+42-2);
    ASSERT_EQ(thawing[2].expiration_epoch,epoch+42-1);
    ASSERT_EQ(thawing[1].expiration_epoch,epoch+42-1);
    ASSERT_EQ(thawing[0].expiration_epoch,epoch+42);


    ASSERT_EQ(thawing[3].target,target);
    ASSERT_EQ(thawing[2].target,target+1);
    ASSERT_EQ(thawing[1].target,target);
    ASSERT_EQ(thawing[0].target,target);

    ASSERT_EQ(thawing[1].amount,t4.amount+t5.amount);
    ASSERT_EQ(thawing[0].amount,0);
    ASSERT_EQ(thawing[2].amount,t6.amount);
    ASSERT_EQ(thawing[3].amount,0);


    //Liability consistency
    auto liability_matches = [&](ThawingFunds& funds)
    {
       bool exists = liability_mgr.Exists(funds.liability_hash,txn);
       if(!exists) return false;

       Liability l = liability_mgr.Get(funds.liability_hash,txn);
       return l.expiration_epoch == funds.expiration_epoch
               && l.amount == funds.amount
               && l.target == funds.target
               && l.source == origin;
    }; 

    for(auto t : thawing)
    {
        ASSERT_TRUE(liability_matches(t));
    }

    //Prune

    //Too early
    ASSERT_EQ(staking_mgr.GetPruneableThawingAmount(origin,info,epoch,txn),0);
    staking_mgr.PruneThawing(origin,info,epoch,txn);
    ASSERT_EQ(info.epoch_thawing_updated,epoch);

    ASSERT_EQ(staking_mgr.GetPruneableThawingAmount(origin,info,epoch,txn),0);

    std::vector<ThawingFunds> thawing2;
    staking_mgr.ProcessThawingFunds(origin,[&](ThawingFunds & funds) {
            thawing2.push_back(funds);
            return true;
            }, txn);

    ASSERT_EQ(thawing2,thawing);
    std::for_each(thawing2.begin(),thawing2.end(),[&](auto t){ ASSERT_TRUE(liability_matches(t));});

    //One epoch too early
    epoch = epoch + 39;
    ASSERT_EQ(staking_mgr.GetPruneableThawingAmount(origin,info,epoch,txn),0);
    ASSERT_EQ(info.epoch_thawing_updated,epoch-39);
    staking_mgr.PruneThawing(origin,info,epoch,txn);
    ASSERT_EQ(info.epoch_thawing_updated,epoch);

    ASSERT_EQ(staking_mgr.GetPruneableThawingAmount(origin,info,epoch,txn),0);

    thawing2.clear();
    staking_mgr.ProcessThawingFunds(origin,[&](ThawingFunds & funds) {
            thawing2.push_back(funds);
            return true;
            }, txn);

    ASSERT_EQ(thawing2,thawing);
    std::for_each(thawing2.begin(),thawing2.end(),[&](auto t){ ASSERT_TRUE(liability_matches(t));});

    //can prune some but not all
    ++epoch;
    ASSERT_EQ(staking_mgr.GetPruneableThawingAmount(origin,info,epoch,txn),t.amount);
    staking_mgr.PruneThawing(origin,info,epoch,txn);
    ASSERT_EQ(info.epoch_thawing_updated,epoch);

    ASSERT_EQ(staking_mgr.GetPruneableThawingAmount(origin,info,epoch,txn),0);

    thawing2.clear();
    staking_mgr.ProcessThawingFunds(origin,[&](ThawingFunds & funds) {
            thawing2.push_back(funds);
            return true;
            }, txn);

    ASSERT_NE(thawing2,thawing);
    thawing.erase(thawing.begin()+3);
    ASSERT_EQ(thawing2,thawing);
    std::for_each(thawing2.begin(),thawing2.end(),[&](auto t){ ASSERT_TRUE(liability_matches(t));});

    //Prune some more
    ++epoch;
    ASSERT_EQ(staking_mgr.GetPruneableThawingAmount(origin,info,epoch,txn),t4.amount+t5.amount+t6.amount);
    ASSERT_EQ(info.epoch_thawing_updated,epoch-1);
    staking_mgr.PruneThawing(origin,info,epoch,txn);
    ASSERT_EQ(info.epoch_thawing_updated,epoch);

    ASSERT_EQ(staking_mgr.GetPruneableThawingAmount(origin,info,epoch,txn),0);

    thawing2.clear();
    staking_mgr.ProcessThawingFunds(origin,[&](ThawingFunds & funds) {
            thawing2.push_back(funds);
            return true;
            }, txn);

    ASSERT_NE(thawing2,thawing);
    thawing.erase(thawing.begin()+1,thawing.end());
    ASSERT_EQ(thawing2,thawing);
    std::for_each(thawing2.begin(),thawing2.end(),[&](auto t){ ASSERT_TRUE(liability_matches(t));});


    //make sure repeated pruning does nothing
    Amount available = info.GetAvailableBalance();
    Amount balance = info.GetBalance();
    ASSERT_EQ(available,starting_available+t4.amount+t5.amount+t6.amount);
    ASSERT_EQ(balance,starting_balance);
    ASSERT_EQ(staking_mgr.GetPruneableThawingAmount(origin,info,epoch,txn),0);
    staking_mgr.PruneThawing(origin,info,epoch,txn);
    ASSERT_EQ(info.GetAvailableBalance(),available);
    ASSERT_EQ(info.GetBalance(),balance);


    //Prune the rest
    ++epoch;
    ASSERT_EQ(staking_mgr.GetPruneableThawingAmount(origin,info,epoch,txn),0);
    staking_mgr.PruneThawing(origin,info,epoch,txn);
    ASSERT_EQ(info.epoch_thawing_updated,epoch);

    ASSERT_EQ(staking_mgr.GetPruneableThawingAmount(origin,info,epoch,txn),0);
    thawing2.clear();
    staking_mgr.ProcessThawingFunds(origin,[&](ThawingFunds & funds) {
            thawing2.push_back(funds);
            return true;
            }, txn);

    ASSERT_EQ(thawing2.size(),0);

    ASSERT_EQ(info.GetAvailableBalance(),available);
    ASSERT_EQ(info.GetBalance(),balance);

    //Try to prune when no thawing funds exist
    ++epoch;
    ASSERT_EQ(staking_mgr.GetPruneableThawingAmount(origin,info,epoch,txn),0);
    staking_mgr.PruneThawing(origin,info,epoch,txn);
    ASSERT_EQ(info.epoch_thawing_updated,epoch);

    ASSERT_EQ(staking_mgr.GetPruneableThawingAmount(origin,info,epoch,txn),0);
    thawing2.clear();
    staking_mgr.ProcessThawingFunds(origin,[&](ThawingFunds & funds) {
            thawing2.push_back(funds);
            return true;
            }, txn);

    ASSERT_EQ(thawing2.size(),0);

    ASSERT_EQ(info.GetAvailableBalance(),available);
    ASSERT_EQ(info.GetBalance(),balance);
}

TEST(Staking_Manager, Frozen)
{

    logos::block_store* store = get_db();
    clear_dbs();
    logos::transaction txn(store->environment,nullptr,true);
   
    AccountAddress origin = 42;
    StakingManager staking_mgr(*store);
    LiabilityManager liability_mgr(*store);
    AccountAddress target = origin;

    uint32_t epoch = 107;

    ThawingFunds t1 = staking_mgr.CreateThawingFunds(origin,origin,epoch,txn);
    t1.amount = 100;
   staking_mgr.Store(t1,origin,txn);

   std::vector<ThawingFunds> thawing(staking_mgr.GetThawingFunds(origin,txn));

   ASSERT_EQ(thawing.size(),1);
   ASSERT_EQ(thawing[0].expiration_epoch,epoch+42);
   LiabilityHash old_liability = t1.liability_hash;
   staking_mgr.MarkThawingAsFrozen(origin,epoch,txn);
   ASSERT_FALSE(liability_mgr.Exists(old_liability,txn));
   thawing = staking_mgr.GetThawingFunds(origin,txn);
   ASSERT_EQ(thawing.size(),1);
   ASSERT_EQ(thawing[0].expiration_epoch,0);
   ASSERT_EQ(thawing[0].amount,t1.amount);
   ASSERT_EQ(thawing[0].target,t1.target);

    //Liability consistency
    auto liability_matches = [&](ThawingFunds& funds)
    {
       bool exists = liability_mgr.Exists(funds.liability_hash,txn);
       if(!exists) return false;

       Liability l = liability_mgr.Get(funds.liability_hash,txn);
       return l.expiration_epoch == funds.expiration_epoch
               && l.amount == funds.amount
               && l.target == funds.target
               && l.source == origin;
    }; 
    ASSERT_TRUE(liability_matches(thawing[0]));

    //make sure frozen is not pruneable
    logos::account_info info;
    info.epoch_thawing_updated = 0;
    ASSERT_EQ(staking_mgr.GetPruneableThawingAmount(origin,info,epoch,txn),0);
    staking_mgr.PruneThawing(origin,info,epoch,txn);


   std::vector<ThawingFunds> thawing2(staking_mgr.GetThawingFunds(origin,txn));
   ASSERT_EQ(thawing,thawing2);

    ++epoch;

    old_liability = thawing[0].liability_hash;
    staking_mgr.SetExpirationOfFrozen(origin,epoch,txn);
    ASSERT_FALSE(liability_mgr.Exists(old_liability,txn));

   thawing = staking_mgr.GetThawingFunds(origin,txn);

   ASSERT_EQ(thawing.size(),1);
   ASSERT_EQ(thawing[0].expiration_epoch,epoch+42);
   ASSERT_EQ(thawing[0].amount,t1.amount);
   ASSERT_EQ(thawing[0].target,t1.target);
 
   ASSERT_TRUE(liability_matches(thawing[0]));

   //make sure won't freeze thawing funds with target != origin
   ++epoch;
   ThawingFunds t2 = staking_mgr.CreateThawingFunds(origin+1,origin,epoch,txn);
   staking_mgr.Store(t2,origin,txn);

   staking_mgr.MarkThawingAsFrozen(origin,epoch,txn);

   thawing = staking_mgr.GetThawingFunds(origin,txn);
   ASSERT_EQ(thawing.size(),2);
   ASSERT_EQ(thawing[0].expiration_epoch,t2.expiration_epoch);
   ASSERT_EQ(thawing[1].expiration_epoch,t2.expiration_epoch-1);



    //mix frozen and unfrozen
    ThawingFunds t3 = staking_mgr.CreateThawingFunds(origin,origin,epoch,txn);
    ThawingFunds t4 = staking_mgr.CreateThawingFunds(origin,origin,epoch+1,txn);
    ThawingFunds t5 = staking_mgr.CreateThawingFunds(origin,origin,epoch+2,txn);

    staking_mgr.Store(t3,origin,txn);
    staking_mgr.Store(t4,origin,txn);
    staking_mgr.Store(t5,origin,txn);

    thawing = staking_mgr.GetThawingFunds(origin,txn);
    ASSERT_EQ(thawing.size(),5);

    staking_mgr.MarkThawingAsFrozen(origin,epoch,txn);

    thawing = staking_mgr.GetThawingFunds(origin,txn);
    ASSERT_EQ(thawing.size(),5);
    ASSERT_EQ(thawing[0].expiration_epoch, t5.expiration_epoch);
    ASSERT_EQ(thawing[1].expiration_epoch, t4.expiration_epoch);
    ASSERT_EQ(thawing[2].expiration_epoch, t2.expiration_epoch);
    ASSERT_EQ(thawing[3].expiration_epoch, t2.expiration_epoch-1);
    ASSERT_EQ(thawing[4].expiration_epoch, 0);

    staking_mgr.MarkThawingAsFrozen(origin,epoch+1,txn);
    thawing = staking_mgr.GetThawingFunds(origin,txn);
    ASSERT_EQ(thawing.size(),4);

    ASSERT_EQ(thawing[0].expiration_epoch, t5.expiration_epoch);
    ASSERT_EQ(thawing[1].expiration_epoch, t2.expiration_epoch);
    ASSERT_EQ(thawing[2].expiration_epoch, t2.expiration_epoch-1);
    ASSERT_EQ(thawing[3].expiration_epoch, 0);

    for(auto t : thawing)
    {
        ASSERT_TRUE(liability_matches(t));
    }
    epoch += 5;
    staking_mgr.SetExpirationOfFrozen(origin,epoch,txn);

    thawing = staking_mgr.GetThawingFunds(origin,txn);
    ASSERT_EQ(thawing.size(),4);

    ASSERT_EQ(thawing[0].expiration_epoch, epoch+42);
    ASSERT_EQ(thawing[1].expiration_epoch, t5.expiration_epoch);
    ASSERT_EQ(thawing[2].expiration_epoch, t2.expiration_epoch);
    ASSERT_EQ(thawing[3].expiration_epoch, t2.expiration_epoch-1);

    for(auto t : thawing)
    {
        ASSERT_TRUE(liability_matches(t));
    }

}

TEST(Staking_Manager, Extract)
{

    clear_dbs();
    logos::block_store* store = get_db();

    logos::transaction txn(store->environment,nullptr,true);
    StakingManager staking_mgr(*store);
    LiabilityManager liability_mgr(*store);

    AccountAddress origin = 73;
    logos::account_info info;
    AccountAddress target = 678;
    AccountAddress target2 = 68780;

    uint32_t epoch = 752;

    auto liability_match = [&](StakedFunds& funds)
    {
        if(liability_mgr.Exists(funds.liability_hash, txn))
        {
            Liability l = liability_mgr.Get(funds.liability_hash,txn);
            EXPECT_EQ(l.amount, funds.amount);
            EXPECT_EQ(l.expiration_epoch, 0);
            EXPECT_EQ(l.target, funds.target);
            EXPECT_EQ(l.source, origin);
            return l.amount == funds.amount
                && l.expiration_epoch == 0
                && l.target == funds.target
                && l.source == origin;
        }
        return false;
    };

    auto liability_matcht =[&](ThawingFunds& funds)
    {
        if(liability_mgr.Exists(funds.liability_hash, txn))
        {
            Liability l = liability_mgr.Get(funds.liability_hash,txn);
            EXPECT_EQ(l.amount, funds.amount);
            EXPECT_EQ(l.expiration_epoch, funds.expiration_epoch);
            EXPECT_EQ(l.target, funds.target);
            EXPECT_EQ(l.source, origin);
            return l.amount == funds.amount
                && l.expiration_epoch == funds.expiration_epoch
                && l.target == funds.target
                && l.source == origin;
        }
        return false;   
    };

    StakedFunds s1 = staking_mgr.CreateStakedFunds(target,origin,txn);
    ASSERT_TRUE(liability_match(s1));
    staking_mgr.UpdateAmountAndStore(s1,origin,100,txn);

    ASSERT_TRUE(liability_match(s1));

    StakedFunds s2 = staking_mgr.CreateStakedFunds(target2,origin,txn);
    ASSERT_TRUE(liability_match(s2));


    auto extract = [&](auto& s1, auto& s2, auto amount)
    {
        staking_mgr.Extract(s1,s2,amount,origin,info, epoch,txn);
        staking_mgr.Store(s2,origin,txn);
    };

    extract(s1,s2,40);


    ASSERT_EQ(s1.amount,60);
    ASSERT_EQ(s2.amount,40);


    ASSERT_TRUE(liability_match(s1));
    ASSERT_TRUE(liability_match(s2));


    ThawingFunds t1 = staking_mgr.CreateThawingFunds(target,origin,epoch,txn);
    ASSERT_EQ(t1.amount,0);
    ASSERT_EQ(t1.expiration_epoch,epoch+42);
    ASSERT_TRUE(liability_matcht(t1));

    extract(s1,t1,15);

    ASSERT_EQ(s1.amount,45);
    ASSERT_EQ(t1.amount,15);

    ASSERT_TRUE(liability_match(s1));
    ASSERT_TRUE(liability_matcht(t1));

    extract(s1,t1,45);

    ASSERT_EQ(s1.amount,0);
    ASSERT_EQ(t1.amount,60);







}


#endif
