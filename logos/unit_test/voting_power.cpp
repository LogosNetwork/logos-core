
#include <gtest/gtest.h>
#include <logos/staking/voting_power_manager.hpp>
#include <logos/unit_test/msg_validator_setup.hpp>
#include <logos/consensus/persistence/epoch/epoch_persistence.hpp>
#include <chrono>
#include <ctime>
#include <ratio>


#define Unit_Test_Voting_Power

#ifdef Unit_Test_Voting_Power

TEST(Voting_Power, SimpleAddAndSubtract)
{
    logos::block_store* store = get_db();
    logos::transaction txn(store->environment, nullptr, true);
    store->clear(store->voting_power_db, txn);
    store->clear(store->representative_db, txn);
    VotingPowerManager voting_power_mgr(*store);

    AccountAddress rep = 42;
    uint32_t epoch = 10;
    Amount self_stake = 1000;
    VotingPowerInfo info;

    RepInfo rep_info;

    store->rep_put(rep, rep_info, txn);

    //Should not exist
    ASSERT_FALSE(voting_power_mgr.GetVotingPowerInfo(rep, info, txn));
    voting_power_mgr.AddSelfStake(rep, self_stake, epoch, txn);
    //Should exist
    ASSERT_TRUE(voting_power_mgr.GetVotingPowerInfo(rep, info, txn));

    //Voting power update is delayed one epoch
    ASSERT_EQ(voting_power_mgr.GetCurrentVotingPower(rep, epoch, txn), 0);

    ++epoch;

    //Should see power update
    ASSERT_EQ(voting_power_mgr.GetCurrentVotingPower(rep, epoch, txn), self_stake);
    
    ++epoch;

    //Power should stay the same
    ASSERT_EQ(voting_power_mgr.GetCurrentVotingPower(rep, epoch, txn), self_stake);

    //Add locked proxied
    Amount locked_proxied = 2000;
    voting_power_mgr.AddLockedProxied(rep, locked_proxied, epoch, txn);

    //Delayed
    ASSERT_EQ(voting_power_mgr.GetCurrentVotingPower(rep, epoch, txn), self_stake);

    ++epoch;

    //Should see power update
    ASSERT_EQ(voting_power_mgr.GetCurrentVotingPower(rep, epoch, txn), self_stake + locked_proxied);

    ++epoch;

    //Power should stay the same
    ASSERT_EQ(voting_power_mgr.GetCurrentVotingPower(rep, epoch, txn), self_stake + locked_proxied);

    Amount unlocked_proxied = 3000;
    Amount diluted_unlocked_proxied = (unlocked_proxied.number() * DILUTION_FACTOR) / 100;
    voting_power_mgr.AddUnlockedProxied(rep, unlocked_proxied, epoch, txn);

    //Delayed
    ASSERT_EQ(voting_power_mgr.GetCurrentVotingPower(rep, epoch, txn), self_stake + locked_proxied);

    ++epoch;

    //Should see power update, with dilution
    ASSERT_EQ(voting_power_mgr.GetCurrentVotingPower(rep, epoch, txn), self_stake + locked_proxied + diluted_unlocked_proxied);


    voting_power_mgr.SubtractUnlockedProxied(rep, unlocked_proxied, epoch, txn);

    //Delayed
    ASSERT_EQ(voting_power_mgr.GetCurrentVotingPower(rep, epoch, txn), self_stake + locked_proxied + diluted_unlocked_proxied);

    ++epoch;

    //should see power update
    ASSERT_EQ(voting_power_mgr.GetCurrentVotingPower(rep, epoch, txn), self_stake + locked_proxied);

    voting_power_mgr.SubtractLockedProxied(rep, locked_proxied, epoch, txn);

    //Delayed
    ASSERT_EQ(voting_power_mgr.GetCurrentVotingPower(rep, epoch, txn), self_stake + locked_proxied);

    ++epoch;

    //should see power update
    ASSERT_EQ(voting_power_mgr.GetCurrentVotingPower(rep, epoch, txn), self_stake);

    voting_power_mgr.SubtractSelfStake(rep, self_stake, epoch, txn);

    //Delayed
    ASSERT_EQ(voting_power_mgr.GetCurrentVotingPower(rep, epoch, txn), self_stake);

    ++epoch;

    ASSERT_EQ(voting_power_mgr.GetCurrentVotingPower(rep, epoch, txn), 0); 
}


TEST(Voting_Power, Pruning)
{
    logos::block_store* store = get_db();
    logos::transaction txn(store->environment, nullptr, true);
    store->clear(store->voting_power_db, txn);
    store->clear(store->representative_db, txn);
    VotingPowerManager voting_power_mgr(*store);

    AccountAddress rep = 42;
    uint32_t epoch = 10;
    Amount self_stake = 1000;
    VotingPowerInfo info;
    RepInfo rep_info;

    //Case 1: Power goes to zero while still rep
    store->rep_put(rep, rep_info, txn);

    voting_power_mgr.AddSelfStake(rep, self_stake, epoch, txn);

    ASSERT_TRUE(voting_power_mgr.GetVotingPowerInfo(rep, info, txn));

    ASSERT_FALSE(voting_power_mgr.CanPrune(rep, info, txn));

    ++epoch;

    voting_power_mgr.SubtractSelfStake(rep, self_stake, epoch, txn);


    ASSERT_TRUE(voting_power_mgr.GetVotingPowerInfo(rep, info, txn));

    //Cant prune if still rep
    ASSERT_FALSE(voting_power_mgr.CanPrune(rep, info, txn));

    store->del(store->representative_db, rep, txn);



    ASSERT_TRUE(voting_power_mgr.CanPrune(rep, info, txn));

    voting_power_mgr.TryPrune(rep, txn);

    //Should no longer exist
    ASSERT_FALSE(voting_power_mgr.GetVotingPowerInfo(rep, info, txn));

    //Test persistence mgr deletion
    store->rep_put(rep, rep_info, txn);
    //Creates VotingPowerInfo with 0 power
    voting_power_mgr.AddSelfStake(rep, 0, epoch, txn);
    ASSERT_TRUE(voting_power_mgr.GetVotingPowerInfo(rep, info, txn));
    PersistenceManager<ECT> epoch_persistence_mgr(*store,nullptr);
    store->rep_mark_remove(rep, txn);
    epoch_persistence_mgr.UpdateRepresentativesDB(txn);

    ASSERT_FALSE(voting_power_mgr.GetVotingPowerInfo(rep, info, txn));



    //Case 2: Power goes to zero after no longer rep

    store->rep_put(rep, rep_info, txn);

    ++epoch;

    voting_power_mgr.AddSelfStake(rep, self_stake, epoch, txn);

    Amount locked_proxied = 2500;
    voting_power_mgr.AddLockedProxied(rep, 2500, epoch, txn);

    ++epoch;

    voting_power_mgr.SubtractSelfStake(rep, self_stake, epoch, txn);

    store->rep_mark_remove(rep, txn);
    epoch_persistence_mgr.UpdateRepresentativesDB(txn);

    //rep removed but VotingPowerInfo not pruned
    ASSERT_TRUE(voting_power_mgr.GetVotingPowerInfo(rep, info, txn));



    ++epoch;

    ASSERT_TRUE(voting_power_mgr.GetVotingPowerInfo(rep, info, txn));

    //Can't prune because still have locked proxied stake
    ASSERT_FALSE(voting_power_mgr.CanPrune(rep, info, txn));
    
    voting_power_mgr.SubtractLockedProxied(rep, locked_proxied, epoch, txn);

    //Pruned during above call 
    ASSERT_FALSE(voting_power_mgr.GetVotingPowerInfo(rep, info, txn));
}


TEST(Voting_Power, ManyProxies)
{

    logos::block_store* store = get_db();
    logos::transaction txn(store->environment, nullptr, true);
    store->clear(store->voting_power_db, txn);
    store->clear(store->representative_db, txn);
    VotingPowerManager voting_power_mgr(*store);

    AccountAddress rep = 42;
    uint32_t epoch = 10;
    Amount self_stake = 1000;
    VotingPowerInfo info;

    RepInfo rep_info;

    store->rep_put(rep, rep_info, txn);
    voting_power_mgr.AddSelfStake(rep, self_stake, epoch, txn);
    ++epoch;

    std::vector<std::pair<Amount,Amount>> proxies;
    Amount total_locked_proxy = 0;
    Amount total_unlocked_proxy = 0;

    for(size_t i = 0; i < 100; ++i)
    {
        proxies.push_back(std::make_pair(i*1000,i*10000));
        total_locked_proxy += proxies[i].first;
        total_unlocked_proxy += proxies[i].second;
        voting_power_mgr.AddLockedProxied(rep, proxies[i].first, epoch, txn);
        voting_power_mgr.AddUnlockedProxied(rep, proxies[i].second, epoch, txn);
        ASSERT_EQ(voting_power_mgr.GetCurrentVotingPower(rep, epoch, txn), self_stake);

        voting_power_mgr.GetVotingPowerInfo(rep, info, txn);
        ASSERT_EQ(info.next.locked_proxied, total_locked_proxy);
        ASSERT_EQ(info.next.unlocked_proxied, total_unlocked_proxy);
    }

    ++epoch;

    Amount power = self_stake + total_locked_proxy
            + (total_unlocked_proxy.number() * DILUTION_FACTOR) / 100;

    ASSERT_EQ(voting_power_mgr.GetCurrentVotingPower(rep, epoch, txn), power);

    for(size_t i = 0; i < proxies.size(); ++i)
    {
        total_locked_proxy -= proxies[i].first;
        total_unlocked_proxy -= proxies[i].second;

        voting_power_mgr.SubtractLockedProxied(rep, proxies[i].first, epoch, txn);
        voting_power_mgr.SubtractUnlockedProxied(rep, proxies[i].second, epoch, txn);

        if(i % 10 == 0)
        {
            ++epoch;

            power = self_stake + total_locked_proxy
                + (total_unlocked_proxy.number() * DILUTION_FACTOR) / 100;
        }

        ASSERT_EQ(voting_power_mgr.GetCurrentVotingPower(rep, epoch, txn), power);

        voting_power_mgr.GetVotingPowerInfo(rep, info, txn);
        ASSERT_EQ(info.next.locked_proxied, total_locked_proxy);
        ASSERT_EQ(info.next.unlocked_proxied, total_unlocked_proxy);
    }
}

TEST(Voting_Power, AccountBalance)
{
    logos::block_store* store = get_db();
    VotingPowerManager voting_power_mgr(*store);
    AccountAddress rep = 0;
    uint32_t epoch = 10;

    {
        logos::transaction txn(store->environment, nullptr, true);
        store->clear(store->voting_power_db, txn);
        store->clear(store->representative_db, txn);
        store->clear(store->account_db, txn);

        RepInfo rep_info;
        store->rep_put(rep, rep_info, txn);

        voting_power_mgr.AddSelfStake(rep, 1000, epoch, txn);
    }



    std::vector<std::pair<AccountAddress,logos::account_info>> accounts;


    {
        logos::transaction txn(store->environment, nullptr, true);

        for(size_t i = 0; i < 1000; ++i)
        {
            AccountAddress add = i;
            logos::account_info info;
            accounts.push_back(std::make_pair(i, info));
            store->account_put(i,info,txn);
        }
    }

    std::cout << "Setting balances" << std::endl;



    std::chrono::steady_clock::time_point start_time 
        = std::chrono::steady_clock::now();

    {

        logos::transaction txn(store->environment, nullptr, true);
        Proxy req;
        req.rep = rep;
        auto proxy_hash = req.Hash();
        store->request_put(req, txn);
        for(size_t i  = 0; i < 1000; ++i)
        {
            accounts[i].second.staking_subchain_head = proxy_hash;
            accounts[i].second.SetBalance(100, epoch, txn);
            ASSERT_EQ(accounts[i].second.GetBalance(),100);
            ASSERT_EQ(accounts[i].second.GetAvailableBalance(),100);
            store->account_put(accounts[i].first, accounts[i].second, txn);
        }
    }

    std::chrono::steady_clock::time_point end_time
        = std::chrono::steady_clock::now();

    std::cout << "Time difference = " 
        << std::chrono::duration_cast<std::chrono::milliseconds> (end_time - start_time).count() <<std::endl;

    std::cout << "Set Balances" << std::endl;

    ++epoch;

    logos::transaction txn(store->environment, nullptr, true);
    VotingPowerInfo vp_info;
    ASSERT_TRUE(voting_power_mgr.GetVotingPowerInfo(rep, vp_info, txn));

    ASSERT_EQ(vp_info.next.unlocked_proxied,100*1000);
    for(size_t i  = 0; i < 1000; ++i)
    {
        accounts[i].second.SetBalance(accounts[i].second.GetBalance() + 100, epoch, txn);
        ASSERT_EQ(accounts[i].second.GetBalance(), 200);
        ASSERT_EQ(accounts[i].second.GetAvailableBalance(), 200);
    }

    ASSERT_TRUE(voting_power_mgr.GetVotingPowerInfo(rep, vp_info, txn));

    ASSERT_EQ(vp_info.current.unlocked_proxied,100*1000);
    ASSERT_EQ(vp_info.next.unlocked_proxied,200*1000);

    for(size_t i  = 0; i < 1000; ++i)
    {
        accounts[i].second.SetBalance(accounts[i].second.GetBalance() - 50, epoch, txn);
        ASSERT_EQ(accounts[i].second.GetBalance(), 150);
        ASSERT_EQ(accounts[i].second.GetAvailableBalance(), 150);
    }

    ASSERT_TRUE(voting_power_mgr.GetVotingPowerInfo(rep, vp_info, txn));
    ASSERT_EQ(vp_info.current.unlocked_proxied,100*1000);
    ASSERT_EQ(vp_info.next.unlocked_proxied,150*1000);


    accounts[0].second.SetAvailableBalance(accounts[0].second.GetBalance() - 50, epoch, txn);
    ASSERT_EQ(accounts[0].second.GetBalance(),150);
    ASSERT_EQ(accounts[0].second.GetAvailableBalance(), 100);
    accounts[0].second.SetBalance(accounts[0].second.GetBalance() - 50, epoch, txn);
    ASSERT_EQ(accounts[0].second.GetBalance(), 100);
    ASSERT_EQ(accounts[0].second.GetAvailableBalance(), 50);

}

#endif
