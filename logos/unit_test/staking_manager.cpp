#include <gtest/gtest.h>
#include <logos/unit_test/msg_validator_setup.hpp>
#include <logos/staking/staking_manager.hpp>
#include <logos/staking/voting_power_manager.hpp>

#define Unit_Test_Staking_Manager

#ifdef Unit_Test_Staking_Manager

TEST(Staking_Manager, Stake)
{
    logos::block_store* store = get_db(); 
    logos::transaction txn(store->environment, nullptr, true);

    store->clear(store->staking_db, txn);
    store->clear(store->thawing_db, txn);
    store->clear(store->account_db, txn);
    store->clear(store->voting_power_db, txn);
    StakingManager staking_mgr(*store);
    VotingPowerManager voting_power_mgr(*store);

    uint32_t epoch = 100;

    logos::account_info info;
    auto set_rep = [&](AccountAddress const & rep)
    {
        Proxy req;
        req.rep = rep;
        req.Hash();
        store->request_put(req,txn);
        info.staking_subchain_head = req.Hash();
    };
    AccountAddress target = 84;
    set_rep(target);
    voting_power_mgr.AddSelfStake(target, 10, epoch, txn);
    Amount initial_balance = 1000;
    info.SetBalance(initial_balance, epoch, txn);
    AccountAddress origin = 42;

    VotingPowerInfo vp_info;
    voting_power_mgr.GetVotingPowerInfo(target, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, 0);
    ASSERT_EQ(vp_info.next.unlocked_proxied, initial_balance);



    store->account_put(origin, info, txn);
    std::vector<ThawingFunds> all_thawing(staking_mgr.GetThawingFunds(origin, txn));

    ASSERT_EQ(all_thawing.size(), 0);

    Amount to_stake = 50;


    staking_mgr.Stake(origin, info, to_stake, target, epoch, txn);

    StakedFunds cur_stake(staking_mgr.GetCurrentStakedFunds(origin, txn));
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

    to_stake += 100;
    staking_mgr.Stake(origin, info, to_stake, target, epoch, txn);

    cur_stake = staking_mgr.GetCurrentStakedFunds(origin, txn);
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

    to_stake -= 50;

    staking_mgr.Stake(origin, info, to_stake, target, epoch, txn);

    all_thawing = staking_mgr.GetThawingFunds(origin, txn);
    ASSERT_EQ(all_thawing.size(), 1);
    ASSERT_EQ(all_thawing[0].target, target);
    ASSERT_EQ(all_thawing[0].amount, 50);
    ASSERT_EQ(all_thawing[0].expiration_epoch,epoch + 42);
    cur_stake = staking_mgr.GetCurrentStakedFunds(origin, txn);
    ASSERT_EQ(cur_stake.amount, to_stake);
    ASSERT_EQ(cur_stake.target, target);
    ASSERT_EQ(info.GetAvailableBalance(),initial_balance - to_stake - 50);
    ASSERT_EQ(info.GetBalance(),initial_balance);

    voting_power_mgr.GetVotingPowerInfo(target, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, to_stake);
    ASSERT_EQ(vp_info.next.unlocked_proxied,info.GetAvailableBalance());

    to_stake -= 25;
    staking_mgr.Stake(origin, info, to_stake, target, epoch, txn);
    all_thawing = staking_mgr.GetThawingFunds(origin, txn);
    ASSERT_EQ(all_thawing.size(), 1);
    ASSERT_EQ(all_thawing[0].target, target);
    ASSERT_EQ(all_thawing[0].amount, 75);
    ASSERT_EQ(all_thawing[0].expiration_epoch,epoch + 42);
    cur_stake = staking_mgr.GetCurrentStakedFunds(origin, txn);
    ASSERT_EQ(cur_stake.amount, to_stake);
    ASSERT_EQ(cur_stake.target, target);
    ASSERT_EQ(info.GetAvailableBalance(),initial_balance - to_stake - 50 - 25);
    ASSERT_EQ(info.GetBalance(),initial_balance);

    voting_power_mgr.GetVotingPowerInfo(target, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, to_stake);
    ASSERT_EQ(vp_info.next.unlocked_proxied,info.GetAvailableBalance());

    target = 85; //change target
    voting_power_mgr.AddSelfStake(target,10,epoch,txn);
    to_stake -= 20;

    staking_mgr.Stake(origin, info, to_stake, target, epoch, txn);
    all_thawing = staking_mgr.GetThawingFunds(origin, txn);
    ASSERT_EQ(all_thawing.size(), 1);
    ASSERT_EQ(all_thawing[0].target, 84);
    ASSERT_EQ(all_thawing[0].amount, 95);
    ASSERT_EQ(all_thawing[0].expiration_epoch,epoch + 42);
    cur_stake = staking_mgr.GetCurrentStakedFunds(origin, txn);
    ASSERT_EQ(cur_stake.amount, to_stake);
    ASSERT_EQ(cur_stake.target, target);
    ASSERT_EQ(info.GetAvailableBalance(),initial_balance - to_stake - 50 - 25 - 20);
    ASSERT_EQ(info.GetBalance(),initial_balance);

    voting_power_mgr.GetVotingPowerInfo(target, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, to_stake);

    to_stake += 50;
    staking_mgr.Stake(origin, info, to_stake, target, epoch, txn);
    all_thawing = staking_mgr.GetThawingFunds(origin, txn);
    ASSERT_EQ(all_thawing.size(), 1);
    ASSERT_EQ(all_thawing[0].target, 84);
    ASSERT_EQ(all_thawing[0].amount, 45);
    ASSERT_EQ(all_thawing[0].expiration_epoch,epoch + 42);
    cur_stake = staking_mgr.GetCurrentStakedFunds(origin, txn);
    ASSERT_EQ(cur_stake.amount, to_stake);
    ASSERT_EQ(cur_stake.target, target);
    ASSERT_EQ(info.GetAvailableBalance(),initial_balance - to_stake - 45);
    ASSERT_EQ(info.GetBalance(),initial_balance);

    voting_power_mgr.GetVotingPowerInfo(target, vp_info, txn);
    ASSERT_EQ(vp_info.next.self_stake, 10);
    ASSERT_EQ(vp_info.next.locked_proxied, to_stake);



}

TEST(Staking_Manager, Thawing)
{
    logos::block_store* store = get_db(); 
    logos::transaction txn(store->environment, nullptr, true);

    store->clear(store->staking_db, txn);
    store->clear(store->thawing_db, txn);
    store->clear(store->account_db, txn);
    store->clear(store->voting_power_db, txn);
    StakingManager staking_mgr(*store);

    AccountAddress origin = 456;
    AccountAddress target = 44;
    uint32_t epoch = 60;
    ThawingFunds t = staking_mgr.CreateThawingFunds(target,origin,epoch,txn);
    staking_mgr.Store(t, origin, txn);
    ++epoch;
    ThawingFunds t2 = staking_mgr.CreateThawingFunds(target,origin,epoch,txn);
    staking_mgr.Store(t2, origin, txn);

    std::vector<ThawingFunds> thawing;
    staking_mgr.IterateThawingFunds(origin,[&](ThawingFunds & funds) {
            thawing.push_back(funds);
            return true;
            }, txn);
    ASSERT_EQ(thawing.size(),2);
    ASSERT_EQ(thawing[0].expiration_epoch,epoch+42-1);
    ASSERT_EQ(thawing[1].expiration_epoch,epoch+42);
}

#endif
