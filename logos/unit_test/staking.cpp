#include <gtest/gtest.h>
#include <logos/unit_test/msg_validator_setup.hpp>
#include <logos/consensus/persistence/persistence.hpp>
#include <logos/consensus/persistence/request/request_persistence.hpp>
#include <logos/consensus/persistence/epoch/epoch_persistence.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
#include <logos/consensus/persistence/reservations.hpp>
#include <logos/request/requests.hpp>
#include <logos/staking/voting_power_manager.hpp>
#include <logos/staking/staking_manager.hpp>


#define Unit_Test_Staking

#ifdef Unit_Test_Staking


extern PrePrepareMessage<ConsensusType::Epoch> create_eb_preprepare(bool t=true);

extern void init_ecies(ECIESPublicKey &ecies);

bool IsStakingRequest(Request const & req)
{
    return req.type == RequestType::StartRepresenting
        || req.type == RequestType::StopRepresenting
        || req.type == RequestType::AnnounceCandidacy
        || req.type == RequestType::RenounceCandidacy
        || req.type == RequestType::Proxy
        || req.type == RequestType::Stake
        || req.type == RequestType::Unstake
        || req.type == RequestType::ElectionVote;
}

TEST(Staking, Basic)
{
    /*
     * This test uses PersistenceManager<R> to
     * Create a rep via StartRepresenting
     * Proxy to that rep from another account
     * Increment the epoch number (epoch transition code is not called)
     * Send funds from proxying account to seperate account
     * Receive funds at proxying account
     * Adjust the amount proxied, and ensure thawing and voting power is updated
     * All throughout, the voting power of the rep is checked for consistency
     */
    logos::block_store* store = get_db();
    clear_dbs();
    std::shared_ptr<Reservations> reservations (std::make_shared<ConsensusReservations>(*store));
    PersistenceManager<R> req_pm(*store, reservations);
    VotingPowerManager vpm = *VotingPowerManager::GetInstance();
    StakingManager sm  = *StakingManager::GetInstance();

    uint32_t epoch_num = 1000;
    EpochVotingManager::ENABLE_ELECTIONS = true;

    AccountAddress account = 123;
    AccountAddress rep = 456;
    bool allow_duplicates = false;


    //init epoch
    auto block = create_eb_preprepare(false);
    AggSignature sig;
    ApprovedEB eb(block, sig, sig);
    eb.epoch_number = epoch_num-1;
    eb.previous = 0;
    {
        logos::transaction txn(store->environment,nullptr,true);
        store->epoch_put(eb,txn);
        store->epoch_tip_put(eb.CreateTip(), txn);
    }


    //init empty accounts
    Amount initial_balance = PersistenceManager<R>::MinTransactionFee(RequestType::Send) * 100;
    Amount initial_rep_balance = PersistenceManager<R>::MinTransactionFee(RequestType::Send) * 500;
    initial_rep_balance += MIN_REP_STAKE;
    logos::account_info info;
    logos::account_info rep_info;
    {
        logos::transaction txn(store->environment, nullptr, true);
        info.SetBalance(initial_balance, 0, txn);
        store->account_put(account, info, txn);
        rep_info.SetBalance(initial_rep_balance, 0, txn);
        store->account_put(rep, rep_info, txn);
    }


    BlockHash rep_prev = 0;
    BlockHash prev = 0;
    BlockHash rep_governance_subchain = 0;
    BlockHash governance_subchain = 0;
    uint32_t seq = 0;
    uint32_t rep_seq = 0;

    auto updateSubchain = [&](Request* req)
    {
        switch(req->type)
        {
            case RequestType::StartRepresenting:
                {
                auto req1 = static_cast<StartRepresenting*>(req);
                req1->governance_subchain_prev = req->origin == rep ? rep_governance_subchain : governance_subchain;
                req1->epoch_num = epoch_num;
                break;
                }
            case RequestType::StopRepresenting:
                {
                auto req1 = static_cast<StopRepresenting*>(req);
                req1->governance_subchain_prev = req->origin == rep ? rep_governance_subchain : governance_subchain;
                req1->epoch_num = epoch_num;
                break;
                }
            case RequestType::AnnounceCandidacy:
                {
                auto req1 = static_cast<AnnounceCandidacy*>(req);
                req1->governance_subchain_prev = req->origin == rep ? rep_governance_subchain : governance_subchain;
                req1->epoch_num = epoch_num;
                break;
                }
            case RequestType::RenounceCandidacy:
                {
                auto req1 = static_cast<RenounceCandidacy*>(req);
                req1->governance_subchain_prev = req->origin == rep ? rep_governance_subchain : governance_subchain;
                req1->epoch_num = epoch_num;
                break;
                }
            case RequestType::Stake:
                {
                auto req1 = static_cast<Stake*>(req);
                req1->governance_subchain_prev = req->origin == rep ? rep_governance_subchain : governance_subchain;
                req1->epoch_num = epoch_num;
                break;
                }
            case RequestType::Unstake:
                {
                auto req1 = static_cast<Unstake*>(req);
                req1->governance_subchain_prev = req->origin == rep ? rep_governance_subchain : governance_subchain;
                req1->epoch_num = epoch_num;
                break;
                }
            case RequestType::Proxy:
                {
                auto req1 = static_cast<Proxy*>(req);
                req1->governance_subchain_prev = req->origin == rep ? rep_governance_subchain : governance_subchain;
                req1->epoch_num = epoch_num;
                break;
                }
            default:
                break;
                //do nothing

        }
    };

    auto validate = [&](auto& req)
    {
        req.fee = PersistenceManager<R>::MinTransactionFee(req.type);
        req.previous = req.origin == rep ? rep_prev : req.origin == account ? prev : 0;
        req.sequence = req.origin == rep ? rep_seq : req.origin == account ? seq : 0;
        if(IsStakingRequest(req))
        {
            updateSubchain(&req);
        }
        req.Hash();
        std::shared_ptr<Request> req_ptr(&req, [](auto r){});
        logos::process_return result;
        bool res = req_pm.ValidateRequest(req_ptr, epoch_num, result, allow_duplicates,false);
        if(!res)
        {
            std::cout << "validate failed. result.code = " << ProcessResultToString(result.code)
                << std::endl;
        }
        return res;
    };


    auto apply = [&](auto req)
    {
        uint64_t timestamp = 0;
        std::shared_ptr<Request> req_ptr(&req, [](auto r){});
        logos::process_return result;
        logos::transaction txn(store->environment, nullptr, true);
        req_pm.ApplyRequest(req_ptr,timestamp,epoch_num,txn);
        if(req.origin == rep)
        {
            rep_prev = req.GetHash();
            rep_seq++;
            if(IsStakingRequest(req))
            {
                rep_governance_subchain = rep_prev;
            }
        }
        else if(req.origin == account)
        {
            prev = req.GetHash();
            seq++;
            if(IsStakingRequest(req))
            {
                governance_subchain = prev;
            }
        }
        store->request_put(req, txn);
    };

    auto update_info = [&]()
    {
        logos::transaction txn(store->environment,nullptr,true);
        store->account_get(account,info,txn);
        store->account_get(rep,rep_info,txn);
    };

    //Create a rep
    StartRepresenting start_rep;
    start_rep.origin = rep;
    start_rep.set_stake = true;
    start_rep.stake = MIN_REP_STAKE;
    ASSERT_TRUE(validate(start_rep));
    apply(start_rep);


    //Proxy to the rep
    Proxy proxy;
    proxy.origin = account;
    proxy.lock_proxy = 100;
    proxy.rep = rep;

    ASSERT_TRUE(validate(proxy));

    apply(proxy);


    update_info();
    {
        logos::transaction txn(store->environment,nullptr,true);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance()); 

        ASSERT_EQ(vp_info.current.self_stake, 0);
        ASSERT_EQ(vp_info.current.locked_proxied, 0);
        ASSERT_EQ(vp_info.current.unlocked_proxied, 0);
    }
    Amount old_bal = info.GetAvailableBalance();

    ++epoch_num;

    {
        logos::transaction txn(store->environment,nullptr,true);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance()); 

        ASSERT_EQ(vp_info.current.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.current.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.current.unlocked_proxied, info.GetAvailableBalance()); 
    }

    //Make sure send updates voting power
    //Note send is empty, but fees harvested
    Send send;
    send.origin = account;

    update_info();
    Amount bal = info.GetAvailableBalance();

    ASSERT_TRUE(validate(send));

    apply(send);
    update_info();

    ASSERT_EQ(info.GetAvailableBalance(),bal-send.fee);


    {
        logos::transaction txn(store->environment,nullptr,true);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance()); 

        ASSERT_EQ(vp_info.current.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.current.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.current.unlocked_proxied, old_bal); 
    }


    //Send with some transactions
    AccountAddress dummy_account = 122222;
    AccountAddress dummy_account2 = 333333;
    {
        logos::transaction txn(store->environment, nullptr, true);
        logos::account_info dummy_info;
        store->account_put(dummy_account,dummy_info,txn);
        store->account_put(dummy_account2,dummy_info,txn);
    }
    send.AddTransaction(dummy_account, PersistenceManager<R>::MinTransactionFee(RequestType::Send) + 4567);
    send.AddTransaction(dummy_account2,3260);
    bal = info.GetAvailableBalance();
    ASSERT_TRUE(validate(send));
    apply(send);

    update_info();

    ASSERT_EQ(info.GetAvailableBalance(),bal-send.GetLogosTotal());


    {
        logos::transaction txn(store->environment,nullptr,true);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
        
        ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance()); 
        

        ASSERT_EQ(vp_info.current.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.current.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.current.unlocked_proxied,old_bal); 
    }


    //Receive some funds
    Send send2;
    send2.origin = dummy_account;
    send2.AddTransaction(account,1000);

    bal = info.GetAvailableBalance();
    ASSERT_TRUE(validate(send2));

    apply(send2);

    update_info();


    ASSERT_EQ(info.GetAvailableBalance(),bal+send2.GetLogosTotal()-send2.fee);


    {
        logos::transaction txn(store->environment,nullptr,true);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
        

        ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance()); 
        

        ASSERT_EQ(vp_info.current.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.current.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.current.unlocked_proxied, old_bal); 
    }

    //decrease stake
   proxy.lock_proxy -= 50;
   old_bal = info.GetAvailableBalance();

   ASSERT_TRUE(validate(proxy));
   apply(proxy);

   update_info();
   ASSERT_EQ(info.GetAvailableBalance(),old_bal-proxy.fee);

    {
        logos::transaction txn(store->environment,nullptr,true);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance());

        std::vector<ThawingFunds> tf = sm.GetThawingFunds(proxy.origin,txn);
        ASSERT_EQ(tf.size(),1);
        ASSERT_EQ(tf[0].amount, 50);
        ASSERT_EQ(tf[0].target, proxy.rep);

        StakedFunds staked;
        sm.GetCurrentStakedFunds(proxy.origin, staked, txn);
        ASSERT_EQ(staked.amount, proxy.lock_proxy);
    }


    //increase stake, uses thawing
    proxy.lock_proxy += 50;
    old_bal = info.GetAvailableBalance();
    ASSERT_TRUE(validate(proxy));
    apply(proxy);

    update_info();
    ASSERT_EQ(info.GetAvailableBalance(), old_bal-proxy.fee);

    {
        logos::transaction txn(store->environment,nullptr,true);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance()); 
        std::vector<ThawingFunds> tf = sm.GetThawingFunds(proxy.origin,txn);
        ASSERT_EQ(tf.size(),0);
    }

    //increase stake, uses available
    proxy.lock_proxy += 50;
    old_bal = info.GetAvailableBalance();
    ASSERT_TRUE(validate(proxy));
    apply(proxy);
    update_info();
    ASSERT_EQ(info.GetAvailableBalance(), old_bal-proxy.fee-50);

    {
        logos::transaction txn(store->environment,nullptr,true);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance()); 
        std::vector<ThawingFunds> tf = sm.GetThawingFunds(proxy.origin,txn);
        ASSERT_EQ(tf.size(),0);
    }

    //decrease stake
    proxy.lock_proxy -= 25;
    old_bal = info.GetAvailableBalance();
    ASSERT_TRUE(validate(proxy));
    apply(proxy);
    update_info();
    ASSERT_EQ(info.GetAvailableBalance(), old_bal-proxy.fee);

    {
        logos::transaction txn(store->environment,nullptr,true);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance()); 
        std::vector<ThawingFunds> tf = sm.GetThawingFunds(proxy.origin,txn);
        ASSERT_EQ(tf.size(),1);
        ASSERT_EQ(tf[0].amount, 25);
        ASSERT_EQ(tf[0].target, proxy.rep);
    }

    //decrease stake, thawing consolidated
    proxy.lock_proxy -= 25;
    old_bal = info.GetAvailableBalance();
    ASSERT_TRUE(validate(proxy));
    apply(proxy);
    update_info();
    ASSERT_EQ(info.GetAvailableBalance(), old_bal-proxy.fee);

    {
        logos::transaction txn(store->environment,nullptr,true);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance()); 
        std::vector<ThawingFunds> tf = sm.GetThawingFunds(proxy.origin,txn);
        ASSERT_EQ(tf.size(),1);
        ASSERT_EQ(tf[0].amount, 50);
        ASSERT_EQ(tf[0].target, proxy.rep);
    }

    ++epoch_num;

    //decrease stake, new thawing
    proxy.lock_proxy -= 10;
    old_bal = info.GetAvailableBalance();
    ASSERT_TRUE(validate(proxy));
    apply(proxy);
    update_info();
    ASSERT_EQ(info.GetAvailableBalance(), old_bal-proxy.fee);

    {
        logos::transaction txn(store->environment,nullptr,true);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance()); 
        std::vector<ThawingFunds> tf = sm.GetThawingFunds(proxy.origin,txn);
        ASSERT_EQ(tf.size(),2);
        ASSERT_EQ(tf[0].amount,10);
        ASSERT_EQ(tf[0].target, proxy.rep);
        ASSERT_EQ(tf[0].expiration_epoch,epoch_num+42);
        ASSERT_EQ(tf[1].amount, 50);
        ASSERT_EQ(tf[1].target, proxy.rep);
        ASSERT_EQ(tf[1].expiration_epoch,epoch_num+41);
    }



    //stake thawing and available
    proxy.lock_proxy += 100;
    old_bal = info.GetAvailableBalance();
    ASSERT_TRUE(validate(proxy));
    apply(proxy);
    update_info();
    ASSERT_EQ(info.GetAvailableBalance(), old_bal-proxy.fee-40);

    {
        logos::transaction txn(store->environment,nullptr,true);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance()); 
        std::vector<ThawingFunds> tf = sm.GetThawingFunds(proxy.origin,txn);
        ASSERT_EQ(tf.size(),0);
    }

    proxy.lock_proxy -= 10;

    ASSERT_TRUE(validate(proxy));
    apply(proxy);
    update_info();

    {
        logos::transaction txn(store->environment,nullptr,true);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance()); 
        std::vector<ThawingFunds> tf = sm.GetThawingFunds(proxy.origin,txn);
        ASSERT_EQ(tf.size(),1);
        ASSERT_EQ(tf[0].amount,10);
        ASSERT_EQ(tf[0].expiration_epoch,epoch_num+42);
    }

    //Attempt to send more than available
    Amount to_send = info.GetAvailableBalance() - proxy.fee;
    Send send3;
    send3.origin = account;
    send3.AddTransaction(dummy_account, to_send);
    //max funds to send
    ASSERT_TRUE(validate(send3));
    send3.AddTransaction(dummy_account, 10);
    //Not enough funds
    ASSERT_FALSE(validate(send3));



    epoch_num += 42;

    //funds should have thawed
    ASSERT_TRUE(validate(send3));

    proxy.lock_proxy = 0;
    ASSERT_TRUE(validate(proxy));
    apply(proxy);
    update_info();

    {
        logos::transaction txn(store->environment,nullptr,true);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance());
    
        StakedFunds f;
        ASSERT_FALSE(sm.GetCurrentStakedFunds(proxy.origin,f,txn));
    }

    proxy.lock_proxy = 10;
    ASSERT_TRUE(validate(proxy));
    apply(proxy);
    update_info();
    {
        logos::transaction txn(store->environment,nullptr,true);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance());
    
        StakedFunds f;
        ASSERT_TRUE(sm.GetCurrentStakedFunds(proxy.origin,f,txn));
    }


}

TEST(Staking, SwitchProxy)
{


    /*
     * This test uses PersistenceManager<R> to create two reps
     * and a single account proxies to the first rep,
     * then switches their proxy to the second rep
     * then switches to a third rep
     * and then switches to a fourth
     */
    logos::block_store* store = get_db();
    clear_dbs();
    std::shared_ptr<Reservations> reservations (std::make_shared<ConsensusReservations>(*store));
    PersistenceManager<R> req_pm(*store, reservations);
    VotingPowerManager vpm = *VotingPowerManager::GetInstance();

    uint32_t epoch_num = 666;
    EpochVotingManager::ENABLE_ELECTIONS = true;


    bool allow_duplicates = false;

    //init epoch
    auto block = create_eb_preprepare(false);
    AggSignature sig;
    ApprovedEB eb(block, sig, sig);
    eb.epoch_number = epoch_num-1;
    eb.previous = 0;
    {
        logos::transaction txn(store->environment,nullptr,true);
        store->epoch_put(eb,txn);
        store->epoch_tip_put(eb.CreateTip(), txn);
    }

    AccountAddress account = 1212871236812;
    AccountAddress rep = 12132819283791273;
    AccountAddress rep2 = 12139976541273;
    AccountAddress rep3 = 435899798764645;
    AccountAddress rep4 = 43546435445;

    //init empty accounts
    Amount initial_balance = PersistenceManager<R>::MinTransactionFee(RequestType::Send) * 100;
    Amount initial_rep_balance = PersistenceManager<R>::MinTransactionFee(RequestType::Send) * 500;
    initial_rep_balance += MIN_REP_STAKE;
    logos::account_info info;
    logos::account_info rep_info;
    {
        logos::transaction txn(store->environment, nullptr, true);
        info.SetBalance(initial_balance, 0, txn);
        store->account_put(account, info, txn);
        rep_info.SetBalance(initial_rep_balance, 0, txn);
        store->account_put(rep, rep_info, txn);
        store->account_put(rep2, rep_info, txn);
        store->account_put(rep3, rep_info, txn);
        store->account_put(rep4, rep_info, txn);
    }






    struct RequestMeta
    {
        BlockHash staking_subchain;
        BlockHash prev;
        uint32_t seq;
        uint32_t epoch_num;


        void FillIn(Request& req, uint32_t epoch)
        {
            req.previous = prev;
            req.sequence = seq;
            epoch_num = epoch;
            if(IsStakingRequest(req))
            {
                updateSubchain(&req);
            }
        }

        void Apply(Request& req)
        {
            prev = req.GetHash();
            if(IsStakingRequest(req))
            {
                staking_subchain = req.GetHash();
            }
            ++seq;
        }

        void updateSubchain(Request* req)
        {
            switch(req->type)
            {
                case RequestType::StartRepresenting:
                    {
                        auto req1 = static_cast<StartRepresenting*>(req);
                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::StopRepresenting:
                    {
                        auto req1 = static_cast<StopRepresenting*>(req);
                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::AnnounceCandidacy:
                    {
                        auto req1 = static_cast<AnnounceCandidacy*>(req);

                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::RenounceCandidacy:
                    {
                        auto req1 = static_cast<RenounceCandidacy*>(req);
                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::Stake:
                    {
                        auto req1 = static_cast<Stake*>(req);
                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::Unstake:
                    {
                        auto req1 = static_cast<Unstake*>(req);
                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::Proxy:
                    {
                        auto req1 = static_cast<Proxy*>(req);
                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                default:
                    break;
                    //do nothing

            }
        }
    };

    std::unordered_map<AccountAddress,RequestMeta> request_meta;


    request_meta[rep] = {0,0,0, epoch_num};
    request_meta[rep2] = {0,0,0, epoch_num};
    request_meta[account] = {0,0,0, epoch_num};

    auto validate = [&](auto& req)
    {
        req.fee = PersistenceManager<R>::MinTransactionFee(req.type);
        request_meta[req.origin].FillIn(req, epoch_num);
        req.Hash();
        std::shared_ptr<Request> req_ptr(&req, [](auto r){});
        logos::process_return result;
        bool res = req_pm.ValidateRequest(req_ptr, epoch_num, result, allow_duplicates,false);
        if(!res)
        {
            std::cout << "validate failed. result.code = " << ProcessResultToString(result.code)
                << std::endl;
        }
        return res;
    };


    auto apply = [&](auto req)
    {
        uint64_t timestamp = 0;
        std::shared_ptr<Request> req_ptr(&req, [](auto r){});
        logos::process_return result;
        logos::transaction txn(store->environment, nullptr, true);
        req_pm.ApplyRequest(req_ptr,timestamp,epoch_num,txn);
        request_meta[req.origin].Apply(req);

        store->request_put(req, txn);
    };

    auto update_info = [&]()
    {
        logos::transaction txn(store->environment,nullptr,true);
        store->account_get(account,info,txn);
        store->account_get(rep,rep_info,txn);
    };

    //Create a rep
    StartRepresenting start_rep;
    start_rep.origin = rep;
    start_rep.set_stake = true;
    start_rep.stake = MIN_REP_STAKE;
    ASSERT_TRUE(validate(start_rep));
    apply(start_rep);

    //Create a second rep
    start_rep.origin = rep2;
    ASSERT_TRUE(validate(start_rep));
    apply(start_rep);
    //Create a third rep
    start_rep.origin = rep3;
    ASSERT_TRUE(validate(start_rep));
    apply(start_rep);
    //Create a fourth rep
    start_rep.origin = rep4;
    ASSERT_TRUE(validate(start_rep));
    apply(start_rep);





    //Proxy to the rep
    Proxy proxy;
    proxy.origin = account;
    proxy.lock_proxy = 100;
    proxy.rep = rep;

    ASSERT_TRUE(validate(proxy));

    apply(proxy);

    update_info();

    {
        logos::transaction txn(store->environment,nullptr,true);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance()); 
        
        ASSERT_EQ(vp_info.current.self_stake, 0);
        ASSERT_EQ(vp_info.current.locked_proxied, 0);
        ASSERT_EQ(vp_info.current.unlocked_proxied, 0);
    }




    ++epoch_num;

    update_info();

    {
        logos::transaction txn(store->environment,nullptr,true);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance()); 
        
        ASSERT_EQ(vp_info.current.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.current.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.current.unlocked_proxied, info.GetAvailableBalance()); 
    }


    //proxy to a new rep
    proxy.rep = rep2;

    ASSERT_TRUE(validate(proxy));

    Amount old_bal = info.GetAvailableBalance();
    apply(proxy);

    update_info();
    ASSERT_EQ(old_bal, info.GetAvailableBalance()+proxy.fee);

    {
        logos::transaction txn(store->environment,nullptr,true);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, 0);
        ASSERT_EQ(vp_info.next.unlocked_proxied, 0); 

        ASSERT_EQ(vp_info.current.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.current.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.current.unlocked_proxied, old_bal); 
    }

    {

        logos::transaction txn(store->environment,nullptr,true);
        VotingPowerInfo vp_info;

        vpm.GetVotingPowerInfo(rep2,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance()); 

        ASSERT_EQ(vp_info.current.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.current.locked_proxied, 0);
        ASSERT_EQ(vp_info.current.unlocked_proxied, 0); 
    }

    //reset the dummy accounts everytime
    AccountAddress dummy_account = 122222;
    AccountAddress dummy_account2 = 333333;
    {
        logos::transaction txn(store->environment, nullptr, true);
        logos::account_info dummy_info;
        store->account_put(dummy_account,dummy_info,txn);
        store->account_put(dummy_account2,dummy_info,txn);
    }
    request_meta[dummy_account] = {0,0,0,epoch_num};
    request_meta[dummy_account2] = {0,0,0,epoch_num};

    auto send_and_receive = [&]()
    {



        //Make sure send updates voting power
        Send send;
        send.origin = account;

        //Send with some transactions

        send.AddTransaction(dummy_account, PersistenceManager<R>::MinTransactionFee(RequestType::Send) + 4567);
        send.AddTransaction(dummy_account2,3260);

        ASSERT_TRUE(validate(send));

        apply(send);
        update_info();


        {
            logos::transaction txn(store->environment,nullptr,true);
            VotingPowerInfo vp_info;
            vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
            ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.next.locked_proxied, 0);
            ASSERT_EQ(vp_info.next.unlocked_proxied, 0); 

            ASSERT_EQ(vp_info.current.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.current.locked_proxied, proxy.lock_proxy);
            ASSERT_EQ(vp_info.current.unlocked_proxied, old_bal); 

            vpm.GetVotingPowerInfo(rep2,epoch_num,vp_info,txn);
            ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
            ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance()); 

            ASSERT_EQ(vp_info.current.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.current.locked_proxied, 0);
            ASSERT_EQ(vp_info.current.unlocked_proxied, 0); 
        }

        //Receive some funds
        Send send2;
        send2.origin = dummy_account;
        send2.AddTransaction(account,1000);

        Amount bal = info.GetAvailableBalance();
        ASSERT_TRUE(validate(send2));

        apply(send2);

        update_info();


        ASSERT_EQ(info.GetAvailableBalance(),bal+send2.GetLogosTotal()-send2.fee);


        {        
            logos::transaction txn(store->environment,nullptr,true);
            VotingPowerInfo vp_info;
            vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
            ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.next.locked_proxied, 0);
            ASSERT_EQ(vp_info.next.unlocked_proxied, 0); 

            ASSERT_EQ(vp_info.current.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.current.locked_proxied, proxy.lock_proxy);
            ASSERT_EQ(vp_info.current.unlocked_proxied, old_bal); 

            vpm.GetVotingPowerInfo(rep2,epoch_num,vp_info,txn);
            ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
            ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance()); 

            ASSERT_EQ(vp_info.current.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.current.locked_proxied, 0);
            ASSERT_EQ(vp_info.current.unlocked_proxied, 0); 
        }

        auto reset_receive = [&](auto key)
        {
            
            logos::transaction txn(store->environment, nullptr, true);
            logos::account_info temp_info;
            store->account_get(key, temp_info, txn);
            temp_info.receive_head = 0;
            store->account_put(key,temp_info,txn);
        };
        reset_receive(account);
        reset_receive(dummy_account);
        reset_receive(dummy_account2);

    };


    send_and_receive();
    send_and_receive();

    ++epoch_num;

    //Proxy to a third rep
    proxy.rep = rep3;

    update_info();

        {        
            logos::transaction txn(store->environment,nullptr,true);
            VotingPowerInfo vp_info;
            vpm.GetVotingPowerInfo(rep2,epoch_num,vp_info,txn);
            ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
            ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance()); 

            ASSERT_EQ(vp_info.current.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.current.locked_proxied, proxy.lock_proxy);
            ASSERT_EQ(vp_info.current.unlocked_proxied, info.GetAvailableBalance()); 

            vpm.GetVotingPowerInfo(rep3,epoch_num,vp_info,txn);
            ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.next.locked_proxied, 0);
            ASSERT_EQ(vp_info.next.unlocked_proxied, 0); 

            ASSERT_EQ(vp_info.current.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.current.locked_proxied, 0);
            ASSERT_EQ(vp_info.current.unlocked_proxied, 0); 
        }


     //make sure used available funds
    old_bal = info.GetAvailableBalance();
    ASSERT_TRUE(validate(proxy));
    apply(proxy);
    update_info();

    Amount staked = proxy.lock_proxy;
    Amount thawing = 0;

    ASSERT_EQ(old_bal, info.GetAvailableBalance()+proxy.fee+proxy.lock_proxy);

    {        
        logos::transaction txn(store->environment,nullptr,true);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(rep2,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, 0);
        ASSERT_EQ(vp_info.next.unlocked_proxied, 0); 

        ASSERT_EQ(vp_info.current.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.current.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.current.unlocked_proxied, old_bal); 

        vpm.GetVotingPowerInfo(rep3,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, proxy.lock_proxy);
        ASSERT_EQ(vp_info.next.unlocked_proxied, info.GetAvailableBalance()); 

        ASSERT_EQ(vp_info.current.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.current.locked_proxied, 0);
        ASSERT_EQ(vp_info.current.unlocked_proxied, 0); 



        std::vector<ThawingFunds> thawing = StakingManager::GetInstance()->GetThawingFunds(account,txn);
        Amount thawing_amt = 0;
        for(auto t : thawing)
        {
            thawing_amt += t.amount;
        }
        ASSERT_EQ(thawing_amt,proxy.lock_proxy);
    }

    proxy.rep = rep4;
    //stake all possible to next rep
    proxy.lock_proxy = info.GetAvailableBalance() + staked + thawing - proxy.fee;
    //fails because some is thawing and can't create new secondary liability
    ASSERT_FALSE(validate(proxy));

    epoch_num += 42;

    //liabilities can be pruned
    ASSERT_TRUE(validate(proxy));
    apply(proxy);


}

TEST(Staking, MultipleProxy)
{
    /*
     * This test creates many accounts, all of which proxy to the same rep
     * Then, those accounts switch their proxy to a new rep
     */
    logos::block_store* store = get_db();
    clear_dbs();
    std::shared_ptr<Reservations> reservations (std::make_shared<ConsensusReservations>(*store));
    PersistenceManager<R> req_pm(*store, reservations);
    VotingPowerManager vpm = *VotingPowerManager::GetInstance();

    uint32_t epoch_num = 666;
    EpochVotingManager::ENABLE_ELECTIONS = true;


    bool allow_duplicates = false;

    //init epoch
    auto block = create_eb_preprepare(false);
    AggSignature sig;
    ApprovedEB eb(block, sig, sig);
    eb.epoch_number = epoch_num-1;
    eb.previous = 0;
    {
        logos::transaction txn(store->environment,nullptr,true);
        store->epoch_put(eb,txn);
        store->epoch_tip_put(eb.CreateTip(), txn);
    }



    Amount initial_balance = PersistenceManager<R>::MinTransactionFee(RequestType::Send) * 100;
    AccountAddress rep = 42;
    
    std::vector<std::pair<AccountAddress,logos::account_info>> accounts;
    for(size_t i = 0; i < 10; ++i)
    {
        AccountAddress address = 1217638716 + (i * 100);
        logos::account_info info;
        logos::transaction txn(store->environment, nullptr, true);
        info.SetBalance(initial_balance, 0, txn);
        store->account_put(address, info, txn);
        accounts.push_back(std::make_pair(address, info));
    } 


    //init empty accounts
    Amount initial_rep_balance = PersistenceManager<R>::MinTransactionFee(RequestType::Send) * 500;
    initial_rep_balance += MIN_REP_STAKE;
    logos::account_info rep_info;
    {
        logos::transaction txn(store->environment, nullptr, true);
        rep_info.SetBalance(initial_rep_balance, 0, txn);
        store->account_put(rep, rep_info, txn);
    }






    struct RequestMeta
    {
        BlockHash staking_subchain;
        BlockHash prev;
        uint32_t seq;
        uint32_t epoch_num;


        void FillIn(Request& req, uint32_t epoch)
        {
            req.previous = prev;
            req.sequence = seq;
            epoch_num = epoch;
            if(IsStakingRequest(req))
            {
                updateSubchain(&req);
            }
        }

        void Apply(Request& req)
        {
            prev = req.GetHash();
            if(IsStakingRequest(req))
            {
                staking_subchain = req.GetHash();
            }
            ++seq;
        }

        void updateSubchain(Request* req)
        {
            switch(req->type)
            {
                case RequestType::StartRepresenting:
                    {
                        auto req1 = static_cast<StartRepresenting*>(req);
                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::StopRepresenting:
                    {
                        auto req1 = static_cast<StopRepresenting*>(req);
                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::AnnounceCandidacy:
                    {
                        auto req1 = static_cast<AnnounceCandidacy*>(req);

                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::RenounceCandidacy:
                    {
                        auto req1 = static_cast<RenounceCandidacy*>(req);
                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::Stake:
                    {
                        auto req1 = static_cast<Stake*>(req);
                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::Unstake:
                    {
                        auto req1 = static_cast<Unstake*>(req);
                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::Proxy:
                    {
                        auto req1 = static_cast<Proxy*>(req);
                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                default:
                    break;
                    //do nothing

            }
        }
    };

    std::unordered_map<AccountAddress,RequestMeta> request_meta;

    for(auto& a : accounts)
    {
        request_meta[a.first] = {0,0,0,epoch_num};
    }


    request_meta[rep] = {0,0,0, epoch_num};

    auto validate = [&](auto& req)
    {
        req.fee = PersistenceManager<R>::MinTransactionFee(req.type);
        request_meta[req.origin].FillIn(req, epoch_num);
        req.Hash();
        std::shared_ptr<Request> req_ptr(&req, [](auto r){});
        logos::process_return result;
        bool res = req_pm.ValidateRequest(req_ptr, epoch_num, result, allow_duplicates,false);
        if(!res)
        {
            std::cout << "validate failed. result.code = " << ProcessResultToString(result.code)
                << std::endl;
        }
        return res;
    };


    auto apply = [&](auto req)
    {
        uint64_t timestamp = 0;
        std::shared_ptr<Request> req_ptr(&req, [](auto r){});
        logos::process_return result;
        logos::transaction txn(store->environment, nullptr, true);
        req_pm.ApplyRequest(req_ptr,timestamp,epoch_num,txn);
        request_meta[req.origin].Apply(req);

        store->request_put(req, txn);
    };

    auto update_info = [&]()
    {
        logos::transaction txn(store->environment,nullptr,true);
        for(auto& a : accounts)
        {
            store->account_get(a.first,a.second,txn);
        }
        store->account_get(rep,rep_info,txn);
    };


    StartRepresenting start_rep;
    start_rep.origin = rep;
    start_rep.stake = MIN_REP_STAKE;
    start_rep.set_stake = true;

    ASSERT_TRUE(validate(start_rep));
    apply(start_rep);

     //second rep
     start_rep.origin = rep+1;
     {
        logos::transaction txn(store->environment, nullptr, true);
        store->account_put(rep+1, rep_info, txn);
    }
     request_meta[rep+1] = {0,0,0,epoch_num};
    ASSERT_TRUE(validate(start_rep));
    apply(start_rep);

    Amount total_lock_proxy = 0;
    Amount total_unlocked_proxy = 0;

    for(auto& a : accounts)
    {
        Proxy proxy;
        proxy.origin = a.first;
        proxy.rep = rep;
        proxy.lock_proxy = 100;
        ASSERT_TRUE(validate(proxy));
        apply(proxy);
        total_lock_proxy += proxy.lock_proxy;
        total_unlocked_proxy += a.second.GetAvailableBalance() - proxy.lock_proxy - proxy.fee;

        {
            logos::transaction txn(store->environment,nullptr,true);
            VotingPowerInfo vp_info;
            vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
            ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.next.locked_proxied, total_lock_proxy);
            ASSERT_EQ(vp_info.next.unlocked_proxied, total_unlocked_proxy); 

            ASSERT_EQ(vp_info.current.self_stake, 0);
            ASSERT_EQ(vp_info.current.locked_proxied, 0);
            ASSERT_EQ(vp_info.current.unlocked_proxied, 0); 
        }
    }


    ++epoch_num;

        {
            logos::transaction txn(store->environment,nullptr,true);
            VotingPowerInfo vp_info;
            vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
            ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.next.locked_proxied, total_lock_proxy);
            ASSERT_EQ(vp_info.next.unlocked_proxied, total_unlocked_proxy); 

            ASSERT_EQ(vp_info.current.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.current.locked_proxied, total_lock_proxy);
            ASSERT_EQ(vp_info.current.unlocked_proxied, total_unlocked_proxy); 
        }



    //adjust amount proxied

    Amount old_lock_proxy = total_lock_proxy;
    Amount old_unlocked_proxy = total_unlocked_proxy;
    for(auto& a : accounts)
    {
        Proxy proxy;
        proxy.origin = a.first;
        proxy.rep = rep;
        proxy.lock_proxy = 50;
        ASSERT_TRUE(validate(proxy));
        apply(proxy);
        total_lock_proxy -= 50;
        total_unlocked_proxy -= proxy.fee;

        {
            logos::transaction txn(store->environment,nullptr,true);
            VotingPowerInfo vp_info;
            vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
            ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.next.locked_proxied, total_lock_proxy);
            ASSERT_EQ(vp_info.next.unlocked_proxied, total_unlocked_proxy); 

            ASSERT_EQ(vp_info.current.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.current.locked_proxied, old_lock_proxy);
            ASSERT_EQ(vp_info.current.unlocked_proxied, old_unlocked_proxy); 
        }
    }

    ++epoch_num;

    //switch to new proxy
    update_info();
    
    Amount total_lock_proxy2 = 0;
    Amount total_unlocked_proxy2 = 0;
    Amount total_fees = 0;
    for(auto& a : accounts)
    {
        Proxy proxy;
        proxy.origin = a.first;
        proxy.rep = rep+1;
        proxy.lock_proxy = 50;
        ASSERT_TRUE(validate(proxy));
        apply(proxy);
        total_lock_proxy2 += 50;
        total_fees += proxy.fee;
        total_unlocked_proxy2 += a.second.GetAvailableBalance() - proxy.fee;

        {
            logos::transaction txn(store->environment,nullptr,true);
            VotingPowerInfo vp_info;
            vpm.GetVotingPowerInfo(rep+1,epoch_num,vp_info,txn);
            ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.next.locked_proxied, total_lock_proxy2);
            ASSERT_EQ(vp_info.next.unlocked_proxied, total_unlocked_proxy2); 

            ASSERT_EQ(vp_info.current.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.current.locked_proxied, 0);
            ASSERT_EQ(vp_info.current.unlocked_proxied, 0); 

            vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
            ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.next.locked_proxied, total_lock_proxy - total_lock_proxy2);
            ASSERT_EQ(vp_info.next.unlocked_proxied, total_unlocked_proxy - total_unlocked_proxy2 - total_fees); 

            ASSERT_EQ(vp_info.current.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.current.locked_proxied, total_lock_proxy);
            ASSERT_EQ(vp_info.current.unlocked_proxied, total_unlocked_proxy); 
        }
    }
}

TEST(Staking, StakeUnstake)
{
    /*
     * This tests the stake and unstake requests for representatives and candidates
     */


    logos::block_store* store = get_db();
    clear_dbs();
    std::shared_ptr<Reservations> reservations (std::make_shared<ConsensusReservations>(*store));
    PersistenceManager<R> req_pm(*store, reservations);
    PersistenceManager<ECT> epoch_pm(*store,nullptr);
    VotingPowerManager vpm = *VotingPowerManager::GetInstance();

    uint32_t epoch_num = 666;
    EpochVotingManager::ENABLE_ELECTIONS = true;


    bool allow_duplicates = false;

    //init epoch
    auto block = create_eb_preprepare(false);
    AggSignature sig;
    ApprovedEB eb(block, sig, sig);
    eb.epoch_number = epoch_num-1;
    eb.previous = 0;
    {
        logos::transaction txn(store->environment,nullptr,true);
        store->epoch_put(eb,txn);
        store->epoch_tip_put(eb.CreateTip(), txn);
    }

    AccountAddress rep = 12132819283791273;

    //init empty accounts
    Amount initial_rep_balance = PersistenceManager<R>::MinTransactionFee(RequestType::Send) * 500;
    initial_rep_balance += MIN_DELEGATE_STAKE;
    logos::account_info rep_info;
    {
        logos::transaction txn(store->environment, nullptr, true);
        rep_info.SetBalance(initial_rep_balance, 0, txn);
        store->account_put(rep, rep_info, txn);
    }






    struct RequestMeta
    {
        BlockHash staking_subchain;
        BlockHash prev;
        uint32_t seq;
        uint32_t epoch_num;


        void FillIn(Request& req, uint32_t epoch)
        {
            req.previous = prev;
            req.sequence = seq;
            epoch_num = epoch;
            if(IsStakingRequest(req))
            {
                std::cout << "is staking request" << std::endl;
                updateSubchain(&req);
            }
        }

        void Apply(Request& req)
        {
            prev = req.GetHash();
            if(IsStakingRequest(req))
            {
                staking_subchain = req.GetHash();
            }
            ++seq;
        }

        void updateSubchain(Request* req)
        {
            switch(req->type)
            {
                case RequestType::StartRepresenting:
                    {
                        auto req1 = static_cast<StartRepresenting*>(req);
                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::StopRepresenting:
                    {
                        auto req1 = static_cast<StopRepresenting*>(req);
                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::AnnounceCandidacy:
                    {
                        auto req1 = static_cast<AnnounceCandidacy*>(req);

                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::RenounceCandidacy:
                    {
                        auto req1 = static_cast<RenounceCandidacy*>(req);
                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::Stake:
                    {
                        auto req1 = static_cast<Stake*>(req);
                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::Unstake:
                    {
                        auto req1 = static_cast<Unstake*>(req);
                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::Proxy:
                    {
                        auto req1 = static_cast<Proxy*>(req);
                        req1->governance_subchain_prev = staking_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                default:
                    break;
                    //do nothing

            }
        }
    };

    std::unordered_map<AccountAddress,RequestMeta> request_meta;


    request_meta[rep] = {0,0,0, epoch_num};

    auto validate = [&](auto& req)
    {
        req.fee = PersistenceManager<R>::MinTransactionFee(RequestType::Send);
        request_meta[req.origin].FillIn(req, epoch_num);
        req.Hash();
        std::cout << "epoch_num of request is " << req.epoch_num << std::endl;
        std::cout << "stored epoch num is " << epoch_num << std::endl;
        std::shared_ptr<Request> req_ptr(&req, [](auto r){});
        logos::process_return result;
        bool res = req_pm.ValidateRequest(req_ptr, epoch_num, result, allow_duplicates,false);
        if(!res)
        {
            std::cout << "validate failed. result.code = " << ProcessResultToString(result.code)
                << std::endl;
        }
        return res;
    };


    auto apply = [&](auto req)
    {
        uint64_t timestamp = 0;
        std::shared_ptr<Request> req_ptr(&req, [](auto r){});
        logos::process_return result;
        logos::transaction txn(store->environment, nullptr, true);
        req_pm.ApplyRequest(req_ptr,timestamp,epoch_num,txn);
        request_meta[req.origin].Apply(req);

        store->request_put(req, txn);
    };

    auto update_info = [&]()
    {
        logos::transaction txn(store->environment,nullptr,true);
        store->account_get(rep,rep_info,txn);
    };

    auto transition_epoch = [&]()
    {
        logos::transaction txn(store->environment,nullptr,true);
        ++epoch_num;
        eb.epoch_number = epoch_num - 1;
        store->epoch_put(eb,txn);
        store->epoch_tip_put(eb.CreateTip(), txn);
    };
    

    //Create a rep

    StartRepresenting start_rep;
    start_rep.origin = rep;
    ASSERT_FALSE(validate(start_rep));

    start_rep.set_stake = true;
    start_rep.stake = MIN_REP_STAKE;

    ASSERT_TRUE(validate(start_rep));

    Amount old_bal = rep_info.GetAvailableBalance();
    apply(start_rep);
    update_info();
    ASSERT_EQ(old_bal, rep_info.GetAvailableBalance()+start_rep.fee+start_rep.stake);


    {
        logos::transaction txn(store->environment,nullptr,true);
            VotingPowerInfo vp_info;
            vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
            ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
            ASSERT_EQ(vp_info.current.self_stake, 0);
    }


    //Adjust their stake
    Stake stake;
    stake.origin = rep;
    stake.stake = MIN_REP_STAKE - 10;
    ASSERT_FALSE(validate(stake));

    stake.stake = MIN_REP_STAKE + 100;
    ASSERT_TRUE(validate(stake));

    old_bal = rep_info.GetAvailableBalance();
    apply(stake);
    update_info();
    ASSERT_EQ(old_bal, rep_info.GetAvailableBalance()+stake.fee+100);

    {
        logos::transaction txn(store->environment,nullptr,true);
            VotingPowerInfo vp_info;
            vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
            ASSERT_EQ(vp_info.next.self_stake, stake.stake);
    }

    stake.stake = MIN_REP_STAKE + 5;
    ASSERT_TRUE(validate(stake));

    old_bal = rep_info.GetAvailableBalance();
    apply(stake);
    update_info();
    ASSERT_EQ(old_bal, rep_info.GetAvailableBalance()+stake.fee);

    {
        logos::transaction txn(store->environment,nullptr,true);
            VotingPowerInfo vp_info;
            vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
            ASSERT_EQ(vp_info.next.self_stake, stake.stake);
    }

    //Make sure thawing was created

    {
        logos::transaction txn(store->environment, nullptr, true);
        std::vector<ThawingFunds> tf = StakingManager::GetInstance()->GetThawingFunds(rep,txn);
        ASSERT_EQ(tf.size(),1);
        ASSERT_EQ(tf[0].amount,95);
    }

    Amount prev_stake = stake.stake;

    transition_epoch();

    {
        logos::transaction txn(store->environment,nullptr,true);
            VotingPowerInfo vp_info;
            vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
            ASSERT_EQ(vp_info.next.self_stake, stake.stake);
            ASSERT_EQ(vp_info.current.self_stake, stake.stake);
    }

    //Make rep candidate
    AnnounceCandidacy announce;
    announce.origin = rep;
    init_ecies(announce.ecies_key);

    ASSERT_FALSE(validate(announce));
    
    stake.stake = MIN_DELEGATE_STAKE;
    ASSERT_TRUE(validate(stake));
    apply(stake);

    //Thawing should be gone
    {
        logos::transaction txn(store->environment, nullptr, true);
        std::vector<ThawingFunds> tf = StakingManager::GetInstance()->GetThawingFunds(rep,txn);
        ASSERT_EQ(tf.size(),0);
    }

    {
        logos::transaction txn(store->environment,nullptr,true);
            VotingPowerInfo vp_info;
            vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
            ASSERT_EQ(vp_info.next.self_stake, stake.stake);
            ASSERT_EQ(vp_info.current.self_stake, prev_stake);
    }

    ASSERT_TRUE(validate(announce));
    apply(announce);
    {
        logos::transaction txn(store->environment,nullptr,true);
            VotingPowerInfo vp_info;
            vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
            ASSERT_EQ(vp_info.next.self_stake, stake.stake);
            ASSERT_EQ(vp_info.current.self_stake, prev_stake);
    }


    stake.stake = MIN_DELEGATE_STAKE - 1;
    ASSERT_FALSE(validate(stake));

    stake.stake = MIN_DELEGATE_STAKE + 10;
    ASSERT_TRUE(validate(stake));

    apply(stake);
    {
        logos::transaction txn(store->environment,nullptr,true);
            VotingPowerInfo vp_info;
            vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
            ASSERT_EQ(vp_info.next.self_stake, stake.stake);
            ASSERT_EQ(vp_info.current.self_stake, prev_stake);
    }

    transition_epoch();

    Unstake unstake;
    unstake.origin = rep;
    ASSERT_FALSE(validate(unstake));

    RenounceCandidacy renounce;
    renounce.origin = rep;
    ASSERT_TRUE(validate(renounce));

    apply(renounce);

    transition_epoch();

    StopRepresenting stop_rep;
    stop_rep.origin = rep;
    ASSERT_TRUE(validate(stop_rep));

    apply(stop_rep);

    stake.stake = MIN_DELEGATE_STAKE -1;
    ASSERT_TRUE(validate(stake));
    stake.stake = MIN_REP_STAKE - 1;
    ASSERT_TRUE(validate(stake));

    ASSERT_TRUE(validate(unstake));
    apply(unstake);


    {
        logos::transaction txn(store->environment,nullptr,true);
            VotingPowerInfo vp_info;
            vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
            ASSERT_EQ(vp_info.next.self_stake,0);
            ASSERT_EQ(vp_info.current.self_stake, MIN_DELEGATE_STAKE+10);
        std::vector<ThawingFunds> tf = StakingManager::GetInstance()->GetThawingFunds(rep,txn);
        ASSERT_EQ(tf.size(),1);
        ASSERT_EQ(tf[0].amount,MIN_DELEGATE_STAKE+10); 
    }

    //simulate account was elected, testing frozen
    //
    block = create_eb_preprepare(false);
    ApprovedEB epoch_block(block, sig, sig);


    epoch_block.epoch_number = epoch_num;
    epoch_block.delegates[0].account = rep;

    {
        logos::transaction txn(store->environment, nullptr, true);
        epoch_pm.UpdateThawing(epoch_block, txn);

        std::vector<ThawingFunds> tf = StakingManager::GetInstance()->GetThawingFunds(rep,txn);

        ASSERT_EQ(tf.size(),1);
        ASSERT_EQ(tf[0].amount,MIN_DELEGATE_STAKE+10);
        ASSERT_EQ(tf[0].expiration_epoch,0);
        store->epoch_put(epoch_block,txn);
        store->epoch_tip_put(epoch_block.CreateTip(), txn);
        epoch_block.delegates[0].account= 0;
        ++epoch_num;
        epoch_block.epoch_number = epoch_num;

        epoch_pm.UpdateThawing(epoch_block, txn);

        tf = StakingManager::GetInstance()->GetThawingFunds(rep,txn);

        ASSERT_EQ(tf.size(),1);
        ASSERT_EQ(tf[0].amount,MIN_DELEGATE_STAKE+10);
        ASSERT_EQ(tf[0].expiration_epoch,epoch_num+42+2);
    }

    start_rep.origin = rep+1;

    {
        logos::transaction txn(store->environment, nullptr, true);
        logos::account_info dummy_info;
        dummy_info.SetBalance(rep_info.GetBalance(),0,txn);
        store->account_put(start_rep.origin, dummy_info, txn);
    }
    ASSERT_TRUE(validate(start_rep));
    apply(start_rep);

    Proxy proxy;
    proxy.origin = rep;
    proxy.rep = start_rep.origin;
    proxy.lock_proxy = 100;
    update_info();
    old_bal = rep_info.GetAvailableBalance();
    ASSERT_TRUE(validate(proxy));
    apply(proxy);
    update_info();

    //Ensure proxy does not use funds previously staked to self
    ASSERT_EQ(rep_info.GetAvailableBalance(), old_bal - proxy.fee - proxy.lock_proxy);
     




}

TEST(Staking, Votes)
{
    logos::block_store* store = get_db();
    clear_dbs();
    std::shared_ptr<Reservations> reservations (std::make_shared<ConsensusReservations>(*store));
    PersistenceManager<R> req_pm(*store, reservations);
    PersistenceManager<ECT> epoch_pm(*store,nullptr);
    VotingPowerManager vpm = *VotingPowerManager::GetInstance();

    uint32_t epoch_num = 666;
    EpochVotingManager::ENABLE_ELECTIONS = true;


    bool allow_duplicates = false;


    //init epoch
    auto block = create_eb_preprepare(false);
    AggSignature sig;
    ApprovedEB eb(block, sig, sig);
    eb.epoch_number = epoch_num-1;
    eb.previous = 0;
    {
        logos::transaction txn(store->environment,nullptr,true);
        store->epoch_put(eb,txn);
        store->epoch_tip_put(eb.CreateTip(), txn);
    }


    AccountAddress rep = 12132819283791273;
    AccountAddress account = 32746238774683;
    AccountAddress candidate = 347823468274382;

    //init empty accounts
    Amount initial_rep_balance = PersistenceManager<R>::MinTransactionFee(RequestType::Send) * 500;
    initial_rep_balance += MIN_DELEGATE_STAKE;
    logos::account_info rep_info;
    logos::account_info info;
    logos::account_info candidate_info;
    {
        logos::transaction txn(store->environment, nullptr, true);
        rep_info.SetBalance(initial_rep_balance, 0, txn);
        info.SetBalance(initial_rep_balance, 0, txn);
        candidate_info.SetBalance(initial_rep_balance, 0, txn);
        store->account_put(rep, rep_info, txn);
        store->account_put(account, info, txn);
        store->account_put(candidate, info, txn);
    }






    struct RequestMeta
    {
        BlockHash governance_subchain;
        BlockHash prev;
        uint32_t seq;
        uint32_t epoch_num;


        void FillIn(Request& req, uint32_t epoch)
        {
            req.previous = prev;
            req.sequence = seq;
            epoch_num = epoch;
            if(IsStakingRequest(req))
            {
                std::cout << "is staking request" << std::endl;
                updateSubchain(&req);
            }
        }

        void Apply(Request& req)
        {
            prev = req.GetHash();
            if(IsStakingRequest(req))
            {
                governance_subchain = req.GetHash();
            }
            ++seq;
        }

        void updateSubchain(Request* req)
        {
            switch(req->type)
            {
                case RequestType::StartRepresenting:
                    {
                        auto req1 = static_cast<StartRepresenting*>(req);
                        req1->governance_subchain_prev = governance_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::StopRepresenting:
                    {
                        auto req1 = static_cast<StopRepresenting*>(req);
                        req1->governance_subchain_prev = governance_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::AnnounceCandidacy:
                    {
                        auto req1 = static_cast<AnnounceCandidacy*>(req);

                        req1->governance_subchain_prev = governance_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::RenounceCandidacy:
                    {
                        auto req1 = static_cast<RenounceCandidacy*>(req);
                        req1->governance_subchain_prev = governance_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::Stake:
                    {
                        auto req1 = static_cast<Stake*>(req);
                        req1->governance_subchain_prev = governance_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::Unstake:
                    {
                        auto req1 = static_cast<Unstake*>(req);
                        req1->governance_subchain_prev = governance_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                case RequestType::Proxy:
                    {
                        auto req1 = static_cast<Proxy*>(req);
                        req1->governance_subchain_prev = governance_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
               case RequestType::ElectionVote:
                    {
                        auto req1 = static_cast<ElectionVote*>(req);
                        req1->governance_subchain_prev = governance_subchain;
                        req1->epoch_num = epoch_num;
                        break;
                    }
                default:
                    break;
                    //do nothing

            }
        }
    };

    std::unordered_map<AccountAddress,RequestMeta> request_meta;


    request_meta[rep] = {0,0,0, epoch_num};

    auto validate = [&](auto& req)
    {
        req.fee = PersistenceManager<R>::MinTransactionFee(req.type);
        request_meta[req.origin].FillIn(req, epoch_num);
        req.Hash();
        std::shared_ptr<Request> req_ptr(&req, [](auto r){});
        logos::process_return result;
        bool res = req_pm.ValidateRequest(req_ptr, epoch_num, result, allow_duplicates,false);
        if(!res)
        {
            std::cout << "validate failed. result.code = " << ProcessResultToString(result.code)
                << std::endl;
        }
        return res;
    };


    auto apply = [&](auto req)
    {
        uint64_t timestamp = 0;
        std::shared_ptr<Request> req_ptr(&req, [](auto r){});
        logos::process_return result;
        logos::transaction txn(store->environment, nullptr, true);

        store->request_put(req, txn);
        req_pm.ApplyRequest(req_ptr,timestamp,epoch_num,txn);
        request_meta[req.origin].Apply(req);

    };

    auto update_info = [&]()
    {
        logos::transaction txn(store->environment,nullptr,true);
        store->account_get(rep,rep_info,txn);
        store->account_get(account, info, txn);
        store->account_get(candidate, candidate_info, txn);
    };

    auto transition_epoch = [&]()
    {
        logos::transaction txn(store->environment,nullptr,true);
        ++epoch_num;
        eb.epoch_number = epoch_num - 1;
        store->epoch_put(eb,txn);
        store->epoch_tip_put(eb.CreateTip(), txn);
        store->clear(store->leading_candidates_db,txn);
        store->leading_candidates_size = 0;
    };


    StartRepresenting start_rep;
    start_rep.origin = rep;
    start_rep.set_stake = true;
    start_rep.stake = MIN_REP_STAKE;

    ASSERT_TRUE(validate(start_rep));
    apply(start_rep);


    Proxy proxy;
    proxy.origin = account;
    proxy.rep = rep;
    proxy.lock_proxy = 100;

    ASSERT_TRUE(validate(proxy));
    apply(proxy);

    AnnounceCandidacy announce;
    announce.origin = candidate;
    announce.set_stake = true;
    announce.stake = MIN_DELEGATE_STAKE;
    init_ecies(announce.ecies_key);

    ASSERT_TRUE(validate(announce));
    apply(announce);

    ElectionVote ev;
    ev.origin = rep;
    ev.votes.emplace_back(candidate,8);
    ASSERT_FALSE(validate(ev));
    transition_epoch();
    ASSERT_TRUE(validate(ev));
    apply(ev);
    update_info();

    Amount total_power = 0;
    {
            logos::transaction txn(store->environment,nullptr,true);
            VotingPowerInfo vp_info;
            vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
            ASSERT_EQ(vp_info.current.self_stake,MIN_REP_STAKE);
            ASSERT_EQ(vp_info.current.locked_proxied, proxy.lock_proxy);
            ASSERT_EQ(vp_info.current.unlocked_proxied, info.GetAvailableBalance());
            Amount diluted = (vp_info.current.unlocked_proxied.number() * DILUTION_FACTOR)
                / 100;
          total_power = diluted + MIN_REP_STAKE + proxy.lock_proxy;  
          ASSERT_EQ(vpm.GetCurrentVotingPower(rep,epoch_num,txn),total_power);
    }

    EpochVotingManager vm(*store);
    std::vector<std::pair<AccountAddress,CandidateInfo>> winners;
    winners = vm.GetElectionWinners(1);

    ASSERT_EQ(winners.size(),1);
    ASSERT_EQ(winners[0].first,candidate);
    ASSERT_EQ(winners[0].second.cur_stake,announce.stake);
    ASSERT_EQ(winners[0].second.votes_received_weighted,total_power*8);

    transition_epoch();

    //change self stake of candidate
    Stake stake;
    stake.origin = candidate;
    stake.stake = MIN_DELEGATE_STAKE + 10;
    ASSERT_TRUE(validate(stake));
    apply(stake);


    ASSERT_TRUE(validate(ev));
    apply(ev);
    update_info();

    winners = vm.GetElectionWinners(1);

    ASSERT_EQ(winners.size(),1);
    ASSERT_EQ(winners[0].first,candidate);
    //uses stake from previous epoch
    ASSERT_EQ(winners[0].second.cur_stake,announce.stake);
    ASSERT_EQ(winners[0].second.votes_received_weighted,total_power*8);
    

    transition_epoch();

    ASSERT_TRUE(validate(ev));
    apply(ev);
    update_info();

    winners = vm.GetElectionWinners(1);

    ASSERT_EQ(winners.size(),1);
    ASSERT_EQ(winners[0].first,candidate);
    //now stake is updated
    ASSERT_EQ(winners[0].second.cur_stake,stake.stake);
    ASSERT_EQ(winners[0].second.votes_received_weighted,total_power*8);


    //Race conditions
    transition_epoch();

    stake.stake = MIN_DELEGATE_STAKE+20;
    ASSERT_TRUE(validate(stake));
    apply(stake);
    //Candidates stake is update to next epoch prior to vote being received
    {
        logos::transaction txn(store->environment, nullptr, true);
        vpm.AddLockedProxied(candidate,100,epoch_num+1,txn);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(candidate,vp_info,txn);
        ASSERT_EQ(vp_info.current.self_stake,MIN_DELEGATE_STAKE+20);
    }
    ASSERT_TRUE(validate(ev));
    apply(ev);
    winners = vm.GetElectionWinners(1);

    ASSERT_EQ(winners.size(),1);
    ASSERT_EQ(winners[0].first,candidate);
    ASSERT_EQ(winners[0].second.cur_stake,MIN_DELEGATE_STAKE+10);
    ASSERT_EQ(winners[0].second.votes_received_weighted,total_power*8);


    transition_epoch();

    //Reps stake is updated prior to voting
    {
        logos::transaction txn(store->environment, nullptr, true);
        vpm.AddLockedProxied(rep,100,epoch_num,txn);
        std::cout << "about to store fallback" << std::endl;
        vpm.AddLockedProxied(rep,100,epoch_num+1,txn);
        std::cout << "stored fallback" << std::endl;
    }
    ASSERT_TRUE(validate(ev));
    apply(ev);
    winners = vm.GetElectionWinners(1);

    ASSERT_EQ(winners.size(),1);
    ASSERT_EQ(winners[0].first,candidate);
    ASSERT_EQ(winners[0].second.cur_stake,stake.stake);
    ASSERT_EQ(winners[0].second.votes_received_weighted,total_power*8);

    


    


}

#endif


