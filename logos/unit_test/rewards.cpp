#include <gtest/gtest.h>

#include <logos/consensus/persistence/request/request_persistence.hpp>
#include <logos/consensus/persistence/epoch/epoch_persistence.hpp>
#include <logos/consensus/persistence/reservations.hpp>
#include <logos/rewards/epoch_rewards_manager.hpp>
#include <logos/unit_test/msg_validator_setup.hpp>
#include <logos/staking/voting_power_manager.hpp>

#define Unit_Test_Rewards

#ifdef Unit_Test_Rewards

static
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

extern PrePrepareMessage<ConsensusType::Epoch> create_eb_preprepare(bool t=true);

extern void init_ecies(ECIESPublicKey &ecies);

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

auto advance_supply = [](auto & block)
{
    auto new_supply = floor(Float100{block.total_supply.number()} * LOGOS_INFLATION_RATE). template convert_to<uint128_t>();
    block.total_supply += new_supply;
};

auto initialize_epoch = [](auto epoch_num, auto store)
{
    auto block = create_eb_preprepare(false);

    AggSignature sig;
    ApprovedEB eb(block, sig, sig);

    eb.epoch_number = epoch_num - 2;
    eb.total_supply = MIN_DELEGATE_STAKE * 10;
    eb.previous = 0;

    logos::transaction txn(store->environment, nullptr, true);
    store->epoch_put(eb, txn);
    store->epoch_tip_put(eb.CreateTip(), txn);

    eb.previous = eb.Hash();
    eb.epoch_number = epoch_num - 1;

    advance_supply(eb);

    store->epoch_put(eb, txn);
    store->epoch_tip_put(eb.CreateTip(), txn);

    return eb;
};

TEST (Rewards, Claim_Processing_1)
{
    logos::block_store* store = get_db();
    clear_dbs();
    std::shared_ptr<Reservations> reservations (std::make_shared<ConsensusReservations>(*store));
    PersistenceManager<R> req_pm(*store, reservations);
    PersistenceManager<ECT> epoch_pm(*store, nullptr);
    VotingPowerManager vpm = *VotingPowerManager::GetInstance();
    auto erm = EpochRewardsManager::GetInstance();

    uint32_t start_epoch = 666;
    uint32_t epoch_num = start_epoch;
    EpochVotingManager::ENABLE_ELECTIONS = true;
    bool allow_duplicates = false;

    // Initialize Epoch
    ApprovedEB eb = initialize_epoch(epoch_num, store);

    AccountAddress rep = 12132819283791273;
    AccountAddress account = 32746238774683;
    AccountAddress candidate = 347823468274382;

    // Initialize empty accounts
    Amount initial_rep_balance = PersistenceManager<R>::MIN_TRANSACTION_FEE * 500;
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
        store->account_put(candidate, candidate_info, txn);
    }

    std::unordered_map<AccountAddress,RequestMeta> request_meta;

    request_meta[rep] = {0,0,0, epoch_num};

    auto validate = [&](auto& req)
    {
        req.fee = PersistenceManager<R>::MIN_TRANSACTION_FEE;
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
        advance_supply(eb);

        epoch_pm.UpdateGlobalRewards(eb, txn);

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

    {
        logos::transaction txn(store->environment, nullptr, true);
        ASSERT_FALSE(erm->GlobalRewardsAvailable(epoch_num, txn));
    }

    ElectionVote ev;
    ev.origin = rep;
    ev.votes.emplace_back(candidate,8);
    ASSERT_FALSE(validate(ev));
    transition_epoch();
    ASSERT_TRUE(validate(ev));
    apply(ev);
    update_info();

    {
        logos::transaction txn(store->environment, nullptr, true);
        ASSERT_TRUE(erm->GlobalRewardsAvailable(epoch_num, txn));
    }

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

    {
        logos::transaction txn(store->environment,nullptr,true);
        ASSERT_FALSE(erm->GlobalRewardsAvailable(epoch_num, txn));
    }

    //change self stake of candidate
    Stake stake;
    stake.origin = candidate;
    stake.stake = MIN_DELEGATE_STAKE + 10;
    ASSERT_TRUE(validate(stake));
    apply(stake);

    ASSERT_TRUE(validate(ev));
    apply(ev);
    update_info();

    {
        logos::transaction txn(store->environment,nullptr,true);
        ASSERT_TRUE(erm->GlobalRewardsAvailable(epoch_num, txn));
    }

    winners = vm.GetElectionWinners(1);

    ASSERT_EQ(winners.size(),1);
    ASSERT_EQ(winners[0].first,candidate);
    //uses stake from previous epoch
    ASSERT_EQ(winners[0].second.cur_stake,announce.stake);
    ASSERT_EQ(winners[0].second.votes_received_weighted,total_power*8);

    transition_epoch();

    {
        logos::transaction txn(store->environment,nullptr,true);
        ASSERT_FALSE(erm->GlobalRewardsAvailable(epoch_num, txn));
    }

    ASSERT_TRUE(validate(ev));
    apply(ev);
    update_info();

    {
        logos::transaction txn(store->environment,nullptr,true);
        ASSERT_TRUE(erm->GlobalRewardsAvailable(epoch_num, txn));
    }

    winners = vm.GetElectionWinners(1);

    ASSERT_EQ(winners.size(),1);
    ASSERT_EQ(winners[0].first,candidate);
    //now stake is updated
    ASSERT_EQ(winners[0].second.cur_stake,stake.stake);
    ASSERT_EQ(winners[0].second.votes_received_weighted,total_power*8);

    //Race conditions
    transition_epoch();

    {
        logos::transaction txn(store->environment,nullptr,true);
        ASSERT_FALSE(erm->GlobalRewardsAvailable(epoch_num, txn));
    }

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

    {
        logos::transaction txn(store->environment,nullptr,true);
        ASSERT_TRUE(erm->GlobalRewardsAvailable(eb.epoch_number, txn));
    }

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

        ASSERT_FALSE(erm->GlobalRewardsAvailable(epoch_num, txn));
    }
    ASSERT_TRUE(validate(ev));
    apply(ev);
    winners = vm.GetElectionWinners(1);

    {
        logos::transaction txn(store->environment,nullptr,true);

        for(uint32_t e = start_epoch + 2; e <= epoch_num; ++e)
        {
            ASSERT_TRUE(erm->GlobalRewardsAvailable(e, txn));
            ASSERT_TRUE(erm->HasRewards(rep, e, txn));
        }
    }

    ASSERT_EQ(winners.size(),1);
    ASSERT_EQ(winners[0].first,candidate);
    ASSERT_EQ(winners[0].second.cur_stake,stake.stake);
    ASSERT_EQ(winners[0].second.votes_received_weighted,total_power*8);

    update_info();

    auto balance = rep_info.GetAvailableBalance();

    Claim claim;
    claim.origin = rep;
    claim.epoch_hash = eb.Hash();
    claim.epoch_number = eb.epoch_number;
    ASSERT_TRUE(validate(claim));
    apply(claim);

    update_info();

    {
        logos::transaction txn(store->environment,nullptr,true);

        ReceiveBlock receive;
        ASSERT_FALSE(store->receive_get(rep_info.receive_head, receive, txn));
        ASSERT_EQ(claim.GetHash(), receive.send_hash);

        Amount pool_diff = 0;

        for(uint32_t e = start_epoch + 1; e <= eb.epoch_number; ++e)
        {
            auto rep_rewards = erm->GetEpochRewardsInfo(rep, e, txn);

            pool_diff += rep_rewards.total_reward -
                         rep_rewards.remaining_reward;

            ASSERT_FALSE(erm->GlobalRewardsAvailable(e, txn));
            ASSERT_TRUE(erm->HasRewards(rep, e, txn));
        }

        pool_diff -= claim.fee;

        auto balance_diff = Amount{rep_info.GetAvailableBalance() - balance}.number();
        ASSERT_EQ(balance_diff, pool_diff.number());
    }

    Amount sum = 0;
    Amount account_balance = info.GetAvailableBalance();

    {
        logos::transaction txn(store->environment,nullptr,true);

        for(uint32_t e = start_epoch + 1; e <= eb.epoch_number; ++e)
        {
            auto rep_rewards = erm->GetEpochRewardsInfo(rep, e, txn);

            sum += rep_rewards.remaining_reward;
        }
        sum -= claim.fee;
    }

    claim.origin = account;
    ASSERT_TRUE(validate(claim));
    apply(claim);

    update_info();

    Amount balance_diff = info.GetAvailableBalance() - account_balance;
    ASSERT_EQ(sum, balance_diff);

    {
        logos::transaction txn(store->environment,nullptr,true);

        ReceiveBlock receive;
        ASSERT_FALSE(store->receive_get(info.receive_head, receive, txn));
        ASSERT_EQ(claim.GetHash(), receive.send_hash);

        for(uint32_t e = start_epoch + 1; e <= eb.epoch_number; ++e)
        {
            ASSERT_FALSE(erm->HasRewards(rep, e, txn));
        }
    }
}

TEST(Staking, Claim_Processing_2)
{
    /*
     * This test creates many accounts, all of which proxy to the same rep
     * Then, those accounts switch their proxy to a new rep
     */
    logos::block_store* store = get_db();
    clear_dbs();
    std::shared_ptr<Reservations> reservations (std::make_shared<ConsensusReservations>(*store));
    PersistenceManager<R> req_pm(*store, reservations);
    PersistenceManager<ECT> epoch_pm(*store, nullptr);
    VotingPowerManager vpm = *VotingPowerManager::GetInstance();
    auto erm = EpochRewardsManager::GetInstance();

    uint32_t epoch_num = 666;
    EpochVotingManager::ENABLE_ELECTIONS = true;
    bool allow_duplicates = false;

    // Initialize Epoch
    ApprovedEB eb = initialize_epoch(epoch_num, store);
    Amount initial_balance = PersistenceManager<R>::MIN_TRANSACTION_FEE * 100;
    AccountAddress rep = 42;
    AccountAddress candidate = 347823468274382;

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

    // Initialize empty accounts
    Amount initial_rep_balance = PersistenceManager<R>::MIN_TRANSACTION_FEE * 500;
    initial_rep_balance += MIN_DELEGATE_STAKE;
    logos::account_info rep_info;
    logos::account_info candidate_info;
    {
        logos::transaction txn(store->environment, nullptr, true);
        rep_info.SetBalance(initial_rep_balance, 0, txn);
        candidate_info.SetBalance(initial_rep_balance, 0, txn);
        store->account_put(rep, rep_info, txn);
        store->account_put(candidate, candidate_info, txn);
    }

    std::unordered_map<AccountAddress,RequestMeta> request_meta;

    for(auto & a : accounts)
    {
        request_meta[a.first] = {0,0,0,epoch_num};
    }

    request_meta[rep] = {0,0,0, epoch_num};

    auto validate = [&](auto& req)
    {
        req.fee = PersistenceManager<R>::MIN_TRANSACTION_FEE;
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
        logos::transaction txn(store->environment, nullptr, true);
        for(auto & a : accounts)
        {
            store->account_get(a.first, a.second, txn);
        }
        store->account_get(rep, rep_info, txn);
        store->account_get(candidate, candidate_info, txn);
    };

    auto transition_epoch = [&]()
    {
        logos::transaction txn(store->environment, nullptr, true);
        ++epoch_num;
        eb.epoch_number = epoch_num - 1;
        advance_supply(eb);

        epoch_pm.UpdateGlobalRewards(eb, txn);

        store->epoch_put(eb,txn);
        store->epoch_tip_put(eb.CreateTip(), txn);
        store->clear(store->leading_candidates_db,txn);
        store->leading_candidates_size = 0;
    };

    StartRepresenting start_rep;
    start_rep.origin = rep;
    start_rep.stake = MIN_REP_STAKE;
    start_rep.set_stake = true;

    ASSERT_TRUE(validate(start_rep));
    apply(start_rep);

    // Create second rep
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
            logos::transaction txn(store->environment, nullptr, true);
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

    AnnounceCandidacy announce;
    announce.origin = candidate;
    announce.set_stake = true;
    announce.stake = MIN_DELEGATE_STAKE;
    init_ecies(announce.ecies_key);

    ASSERT_TRUE(validate(announce));
    apply(announce);

    {
        logos::transaction txn(store->environment, nullptr, true);
        ASSERT_FALSE(erm->GlobalRewardsAvailable(epoch_num, txn));
    }

    ElectionVote ev;
    ev.origin = rep;
    ev.votes.emplace_back(candidate,8);
    ASSERT_FALSE(validate(ev));
    transition_epoch();
    ASSERT_TRUE(validate(ev));
    apply(ev);
    update_info();

    ev.origin = rep + 1;
    ASSERT_TRUE(validate(ev));
    apply(ev);
    update_info();

    transition_epoch();

    {
        logos::transaction txn(store->environment, nullptr, true);

        ASSERT_TRUE(erm->GlobalRewardsAvailable(epoch_num - 1, txn));
        ASSERT_TRUE(erm->HasRewards(rep, epoch_num - 1, txn));
    }

    {
        logos::transaction txn(store->environment, nullptr, true);
        VotingPowerInfo vp_info;
        vpm.GetVotingPowerInfo(rep,epoch_num,vp_info,txn);
        ASSERT_EQ(vp_info.next.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.next.locked_proxied, total_lock_proxy);
        ASSERT_EQ(vp_info.next.unlocked_proxied, total_unlocked_proxy);

        ASSERT_EQ(vp_info.current.self_stake, start_rep.stake);
        ASSERT_EQ(vp_info.current.locked_proxied, total_lock_proxy);
        ASSERT_EQ(vp_info.current.unlocked_proxied, total_unlocked_proxy);
    }

    // Adjust amount proxied
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
            logos::transaction txn(store->environment, nullptr, true);
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

    ev.origin = rep;
    ASSERT_TRUE(validate(ev));
    apply(ev);
    update_info();

    ev.origin = rep + 1;
    ASSERT_TRUE(validate(ev));
    apply(ev);
    update_info();

    transition_epoch();

    {
        logos::transaction txn(store->environment, nullptr, true);

        ASSERT_TRUE(erm->GlobalRewardsAvailable(epoch_num - 1, txn));
        ASSERT_TRUE(erm->HasRewards(rep, epoch_num - 1, txn));
        ASSERT_TRUE(erm->HasRewards(rep + 1, epoch_num - 1, txn));
    }

    update_info();

    // Switch to new proxy
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

    ev.origin = rep;
    ASSERT_TRUE(validate(ev));
    apply(ev);
    update_info();

    ev.origin = rep + 1;
    ASSERT_TRUE(validate(ev));
    apply(ev);
    update_info();

    transition_epoch();

    ev.origin = rep;
    ASSERT_TRUE(validate(ev));
    apply(ev);
    update_info();

    ev.origin = rep + 1;
    ASSERT_TRUE(validate(ev));
    apply(ev);
    update_info();

    transition_epoch();

    for(size_t i = 0; i < 10; ++i)
    {
        AccountAddress address = 1217638716 + (i * 100);

        Claim claim;
        claim.origin = address;
        claim.epoch_hash = eb.Hash();
        claim.epoch_number = eb.epoch_number;
        ASSERT_TRUE(validate(claim));
        apply(claim);

        logos::transaction txn(store->environment, nullptr, true);

        logos::account_info info;
        store->account_get(address, info, txn);

        ReceiveBlock receive;
        ASSERT_FALSE(store->receive_get(info.receive_head, receive, txn));
        ASSERT_EQ(claim.GetHash(), receive.send_hash);
    }

    {
        logos::transaction txn(store->environment, nullptr, true);

        ASSERT_TRUE(erm->GlobalRewardsAvailable(epoch_num - 1, txn));
        ASSERT_TRUE(erm->HasRewards(rep, epoch_num - 1, txn));
        ASSERT_TRUE(erm->HasRewards(rep + 1, epoch_num - 1, txn));
    }

    Claim claim;
    claim.origin = rep;
    claim.epoch_hash = eb.Hash();
    claim.epoch_number = eb.epoch_number;
    ASSERT_TRUE(validate(claim));
    apply(claim);

    {
        logos::transaction txn(store->environment, nullptr, true);

        ASSERT_FALSE(erm->GlobalRewardsAvailable(epoch_num - 1, txn));
        ASSERT_FALSE(erm->HasRewards(rep, epoch_num - 1, txn));
        ASSERT_TRUE(erm->HasRewards(rep + 1, epoch_num - 1, txn));
    }

    claim.origin = rep + 1;
    claim.epoch_hash = eb.Hash();
    claim.epoch_number = eb.epoch_number;
    ASSERT_TRUE(validate(claim));
    apply(claim);

    {
        logos::transaction txn(store->environment, nullptr, true);

        ASSERT_FALSE(erm->GlobalRewardsAvailable(epoch_num - 1, txn));
        ASSERT_FALSE(erm->HasRewards(rep, epoch_num - 1, txn));
        ASSERT_FALSE(erm->HasRewards(rep + 1, epoch_num - 1, txn));
    }
}

#endif // #ifdef Unit_Test_Rewards
