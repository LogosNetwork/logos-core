#include <gtest/gtest.h>
#include <logos/blockstore.hpp>
#include <logos/unit_test/msg_validator_setup.hpp>
#include <logos/governance/requests.hpp>
#include <logos/common.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
#include <logos/elections/candidate.hpp>
#include <logos/elections/representative.hpp>
#include <logos/identity_management/delegate_identity_manager.hpp>
#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/persistence/persistence.hpp>
#include <logos/staking/staking_manager.hpp>
#include <logos/staking/voting_power_manager.hpp>

#define Unit_Test_Elections

#ifdef Unit_Test_Elections

extern Delegate init_delegate(AccountAddress account, Amount vote, Amount stake, bool starting_term);
extern void init_ecies(ECIESPublicKey &ecies);
extern PrePrepareMessage<ConsensusType::Epoch> create_eb_preprepare(bool t=true);


void init_tips(uint32_t epoch_num)
{

    logos::block_store* store = get_db();
   
    logos::transaction txn(store->environment, nullptr, true);
    for(uint8_t del = 0; del < NUM_DELEGATES; ++del)
    {
        Tip dummy;
        std::cout << "Writing tip for del " << del << " in epoch " << epoch_num << std::endl;
        assert(!store->request_tip_put(del, epoch_num, dummy, txn));
    }

}

TEST (Elections, blockstore)
{
    logos::block_store* store(get_db());
    clear_dbs();
    {
        logos::transaction txn(store->environment,nullptr,true);

        //Generic request
        Request req;
        req.type = RequestType::Unknown;
        req.Hash();
        bool res = store->request_put(req,txn);
        ASSERT_FALSE(res);

        Request req2;
        req2.type = RequestType::Unknown;
        res = store->request_get(req.Hash(),req2,txn);

        ASSERT_FALSE(res);
        ASSERT_EQ(req,req2);

        //ElectionVote no votes
        BlockHash prev = 111;
        AccountAddress address = 1;
        AccountSig sig = 1;
        Amount fee = 7;
        uint32_t sequence = 2;
        ElectionVote ev;
        ev.origin = address;
        ev.previous = prev;
        ev.fee = fee;
        ev.sequence = sequence;
        ev.signature = sig;
        ev.epoch_num = 42;

        
        auto hash = ev.Hash();
        res = store->request_put(ev,txn);

        ASSERT_FALSE(res);

        ElectionVote ev2;
        ev2.type = RequestType::ElectionVote;
        res = store->request_get(hash,ev2,txn);
        ASSERT_FALSE(res);
        ASSERT_EQ(ev,ev2);

        ElectionVote ev_json(res, ev.SerializeJson());
        ASSERT_FALSE(res);
        ASSERT_EQ(ev_json, ev);

        //Election vote with 3 votes
        ElectionVote::CandidateVotePair p1(1,8);
        ElectionVote::CandidateVotePair p2(2,12);
        ElectionVote::CandidateVotePair p3(3,5);
        std::vector<ElectionVote::CandidateVotePair> votes = {p1,p2,p3};
        ev.votes = votes;
        ev.origin = 12;

        hash = ev.Hash();
        res = store->request_put(ev,txn);
        ASSERT_FALSE(res);

        ElectionVote ev3;
        ev3.type = RequestType::ElectionVote;
        res = store->request_get(ev.Hash(),ev3,txn);
        ASSERT_FALSE(res);
        ASSERT_EQ(ev,ev3);
        ASSERT_NE(ev3,ev2);

        ev_json = ElectionVote(res, ev3.SerializeJson());
        ASSERT_FALSE(res);
        ASSERT_EQ(ev_json, ev3);


        AnnounceCandidacy announce;
        init_ecies(announce.ecies_key);
        announce.origin = 7;
        announce.previous = 12;
        announce.sequence = 23;
        announce.fee = 2;
        announce.signature = 7;
        announce.stake = 4;
        announce.set_stake = true;
        announce.bls_key = 13;
        announce.epoch_num = 11;
        announce.governance_subchain_prev = 4267;
        announce.Hash();

        ASSERT_FALSE(store->request_put(announce,txn));
        AnnounceCandidacy announce2;
        init_ecies(announce2.ecies_key);
        ASSERT_FALSE(store->request_get(announce.Hash(),announce2,txn));
        ASSERT_EQ(announce2.type,RequestType::AnnounceCandidacy);
        ASSERT_EQ(announce.stake,announce2.stake);
        ASSERT_EQ(announce.ecies_key,announce2.ecies_key);
        ASSERT_EQ(announce.set_stake,announce2.set_stake);
        ASSERT_EQ(announce.bls_key,announce2.bls_key);
        ASSERT_EQ(announce.epoch_num,announce2.epoch_num);
        ASSERT_EQ(announce.governance_subchain_prev,announce2.governance_subchain_prev);
        ASSERT_EQ(announce,announce2);

        AnnounceCandidacy announce_json(res, announce.SerializeJson());
        init_ecies(announce_json.ecies_key);
        ASSERT_FALSE(res);
        ASSERT_EQ(announce_json, announce);

        RenounceCandidacy renounce;
        renounce.origin = 2;
        renounce.previous = 3;
        renounce.sequence = 5;
        renounce.signature = 7;
        renounce.epoch_num = 26;
        renounce.governance_subchain_prev = 23489;
        renounce.Hash();
        ASSERT_FALSE(store->request_put(renounce,txn));
        RenounceCandidacy renounce2;
        ASSERT_FALSE(store->request_get(renounce.Hash(),renounce2,txn));
        ASSERT_EQ(renounce,renounce2);
        RenounceCandidacy renounce_json(res, renounce.SerializeJson());
        
        ASSERT_FALSE(res);

        ASSERT_EQ(renounce_json, renounce);

        StartRepresenting start;
        start.origin = 4;
        start.previous = 5;
        start.sequence = 2;
        start.fee = 3;
        start.stake = 32;
        start.set_stake = true;
        start.epoch_num = 456;
        start.governance_subchain_prev = 10000654;
        start.Hash();
        ASSERT_FALSE(store->request_put(start,txn));
        StartRepresenting start2;
        ASSERT_EQ(GetRequestType<StartRepresenting>(),RequestType::StartRepresenting);
        ASSERT_FALSE(store->request_get(start.Hash(),start2,txn));
        ASSERT_EQ(start.stake,start2.stake);
        ASSERT_EQ(start,start2);

        StartRepresenting start_json(res, start.SerializeJson());
        ASSERT_FALSE(res);

        ASSERT_EQ(start_json, start);


        StopRepresenting stop;
        stop.origin = 4;
        stop.previous = 5;
        stop.sequence = 47;
        stop.fee = 12;
        stop.epoch_num = 456;
        stop.governance_subchain_prev = 7789;
        stop.Hash();
        ASSERT_FALSE(store->request_put(stop,txn));
        StopRepresenting stop2;
        ASSERT_EQ(GetRequestType<StopRepresenting>(),RequestType::StopRepresenting);
        ASSERT_FALSE(store->request_get(stop.Hash(),stop2,txn));
        ASSERT_EQ(stop,stop2);

        StopRepresenting stop_json(res, stop.SerializeJson());


        RepInfo rep_info;
        AccountAddress rep_account = 1;
        rep_info.election_vote_tip = ev.Hash();
        rep_info.candidacy_action_tip = announce.Hash();
        rep_info.rep_action_tip = start.Hash();

        res = store->rep_put(rep_account,rep_info,txn);
        ASSERT_FALSE(res);

        RepInfo rep_info2;
        res = store->rep_get(rep_account,rep_info2,txn);
        ASSERT_FALSE(res);
        ASSERT_EQ(rep_info,rep_info2);


        CandidateInfo candidate_info;
        init_ecies(candidate_info.ecies_key);
        AccountAddress candidate_account;
        candidate_info.cur_stake = 42;
        candidate_info.next_stake = 5000;
        candidate_info.bls_key = 3;
        candidate_info.epoch_modified = 67;

        ASSERT_FALSE(store->candidate_put(candidate_account,candidate_info,txn));

        CandidateInfo candidate_info2;
        init_ecies(candidate_info2.ecies_key);
        ASSERT_FALSE(store->candidate_get(candidate_account,candidate_info2,txn));
        ASSERT_EQ(candidate_info,candidate_info2);
    }
   
}

TEST(Elections, candidates_simple)
{
    
    logos::block_store* store = get_db();
    clear_dbs();
    
    CandidateInfo c1(100);
    init_ecies(c1.ecies_key);
    c1.cur_stake = 34;
    c1.next_stake = 45;
    c1.bls_key = 4;
    c1.epoch_modified = 12;
    AccountAddress a1(0);
    CandidateInfo c2(110);
    init_ecies(c2.ecies_key);
    c2.cur_stake = 456;
    c2.next_stake = 123;
    c2.bls_key = 7;
    c2.epoch_modified = 96;
    AccountAddress a2(1);

    logos::transaction txn(store->environment,nullptr,true);
    {
        bool res = store->candidate_put(a1,c1,txn);
        ASSERT_FALSE(res);
        res = store->candidate_put(a2,c2,txn);
        ASSERT_FALSE(res);

        CandidateInfo c1_copy;
        init_ecies(c1_copy.ecies_key);
        res = store->candidate_get(a1,c1_copy,txn);
        ASSERT_FALSE(res);
        ASSERT_EQ(c1,c1_copy);

        CandidateInfo c2_copy;
        init_ecies(c2_copy.ecies_key);
        res = store->candidate_get(a2,c2_copy,txn);
        ASSERT_FALSE(res);
        ASSERT_EQ(c2,c2_copy);

        VotingPowerManager::GetInstance()->AddSelfStake(a1,c1.cur_stake,c1.epoch_modified-1,txn);
        res = store->candidate_add_vote(a1,100,c1.epoch_modified,txn);
        ASSERT_FALSE(res);
        res = store->candidate_add_vote(a1,50,c1.epoch_modified,txn);
        ASSERT_FALSE(res);

        CandidateInfo c3_copy;
        init_ecies(c3_copy.ecies_key);
        res = store->candidate_get(a1,c3_copy,txn);
        ASSERT_FALSE(res);
        ASSERT_EQ(c3_copy.votes_received_weighted,c1.votes_received_weighted+100+50);
        ASSERT_EQ(c3_copy.epoch_modified,c1.epoch_modified);

        ASSERT_FALSE(store->candidate_add_vote(a1,70,c1.epoch_modified+1,txn));

        ASSERT_FALSE(store->candidate_get(a1,c3_copy,txn));
        ASSERT_EQ(c3_copy.votes_received_weighted,70);
        ASSERT_EQ(c3_copy.epoch_modified,c1.epoch_modified+1);

        ASSERT_FALSE(store->candidate_add_vote(a1,40,c1.epoch_modified+40,txn));
        ASSERT_FALSE(store->candidate_get(a1,c3_copy,txn));
        ASSERT_EQ(c3_copy.votes_received_weighted,40);
        ASSERT_EQ(c3_copy.epoch_modified,c1.epoch_modified+40);

        
        AccountAddress a3(2);
        ASSERT_TRUE(store->candidate_add_vote(a3,100,0,txn));
    }
}

TEST(Elections, get_winners)
{

    logos::block_store* store = get_db();
    clear_dbs();

    EpochVotingManager mgr(*store);

    size_t num_winners = 8;
    auto winners = mgr.GetElectionWinners(num_winners);
    ASSERT_EQ(winners.size(),0);
    
    std::vector<std::pair<AccountAddress,CandidateInfo>> candidates;
    size_t num_candidates = 100;
    for(size_t i = 0; i < num_candidates; ++i)
    {
        logos::transaction txn(store->environment,nullptr,true);
        CandidateInfo c((i % 3) * 100 + i);
        init_ecies(c.ecies_key);
        c.bls_key = i * 4 + 37;
        AccountAddress a(i);
        store->candidate_put(a,c,txn);
        candidates.push_back(std::make_pair(a,c));
    }
    std::sort(candidates.begin(), candidates.end(),
            [&store](auto p1, auto p2) 
            {
                return store->candidate_is_greater(p1.first,p1.second,p2.first,p2.second); 
            });

    std::vector<std::pair<AccountAddress,CandidateInfo>> results(
            candidates.begin(),candidates.begin() + num_winners);
    
    winners = mgr.GetElectionWinners(num_winners);

    std::sort(winners.begin(), winners.end(),
            [&store](auto p1, auto p2) 
            {
                return store->candidate_is_greater(p1.first,p1.second,p2.first,p2.second); 
            });

    ASSERT_EQ(winners.size(),results.size());

    ASSERT_EQ(winners,results);
    
}

void iterateCandidatesDB(
        logos::block_store& store,
        std::function<void(logos::store_iterator& it)> func,
        MDB_txn* txn)
{
    for(auto it = logos::store_iterator(txn,store.candidacy_db);
            it != logos::store_iterator(nullptr); ++it)
    {
        func(it);
    }
}

TEST(Elections,candidates_transition)
{
    logos::block_store* store = get_db();
    clear_dbs();

    AccountAddress a1(0);
    AccountAddress a2(1);
    AccountAddress a3(2);
    DelegatePubKey bls1(0);
    DelegatePubKey bls2(1);
    DelegatePubKey bls3(2);
    Amount stake1(1);
    Amount stake2(2);
    Amount stake3(3);

    PersistenceManager<ECT> mgr(*store,nullptr);

    logos::transaction txn(store->environment,nullptr,true);
    VotingPowerManager::GetInstance()->AddSelfStake(a1,stake1,0,txn);
    VotingPowerManager::GetInstance()->AddSelfStake(a2,stake2,0,txn);
    VotingPowerManager::GetInstance()->AddSelfStake(a3,stake3,0,txn);
    {
        CandidateInfo candidate;
        init_ecies(candidate.ecies_key);
        candidate.cur_stake = stake1;
        candidate.next_stake = stake1;
        candidate.bls_key = bls1;
        ASSERT_FALSE(store->candidate_put(a1, candidate, txn));
        candidate.cur_stake = stake2;
        candidate.next_stake = stake2;
        candidate.bls_key = bls2;
        ASSERT_FALSE(store->candidate_put(a2, candidate, txn));
    }

    iterateCandidatesDB(*store,[](auto& it){
            bool error = false;
            CandidateInfo info(error,it->second);
            init_ecies(info.ecies_key);
            ASSERT_FALSE(error);
            ASSERT_EQ(info.votes_received_weighted,0);
            },txn);       

    {
        bool res = store->candidate_mark_remove(a1,txn);
        ASSERT_FALSE(res);
        CandidateInfo info;
        init_ecies(info.ecies_key);
        ASSERT_FALSE(store->candidate_get(a1,info,txn));
        CandidateInfo candidate;
        init_ecies(candidate.ecies_key);
        candidate.cur_stake = stake3;
        candidate.next_stake = stake3;
        candidate.bls_key = bls3;
        ASSERT_FALSE(store->candidate_put(a3, candidate, txn));
    }

    mgr.UpdateCandidatesDB(txn);

    {
        CandidateInfo info;
        init_ecies(info.ecies_key);
        bool res = store->candidate_get(a1,info,txn);
        ASSERT_TRUE(res);
        res = store->candidate_get(a2,info,txn);
        ASSERT_FALSE(res);
        res = store->candidate_get(a3,info,txn);
        ASSERT_FALSE(res);
    }


    {
        auto block = create_eb_preprepare(false);
        AggSignature sig;
        ApprovedEB eb(block, sig, sig);
        eb.delegates[0].account = a2;
        eb.delegates[0].starting_term = true;

        ASSERT_FALSE(store->epoch_put(eb,txn));
        ASSERT_FALSE(store->epoch_tip_put(eb.CreateTip(),txn));

    }    

    mgr.MarkDelegateElectsAsRemove(txn);
    mgr.UpdateCandidatesDB(txn);


    {
        CandidateInfo info;
        init_ecies(info.ecies_key);
        bool res = store->candidate_get(a2,info,txn);
        ASSERT_TRUE(res);
        res = store->candidate_get(a3,info,txn);
        ASSERT_FALSE(res);
    }

    {

        auto block = create_eb_preprepare(false);
        AggSignature sig;
        ApprovedEB eb(block, sig, sig);
        {
            Tip tip;
            ASSERT_FALSE(store->epoch_tip_get(tip,txn));
            eb.previous = tip.digest;
            eb.delegates[0].starting_term = false;
            eb.delegates[1].starting_term = true;
            ASSERT_FALSE(store->epoch_put(eb,txn));
            eb.previous = eb.Hash();
            ASSERT_FALSE(store->epoch_put(eb,txn));
            ASSERT_FALSE(store->epoch_tip_put(eb.CreateTip(),txn));
        }

        {
            CandidateInfo info;
            init_ecies(info.ecies_key);
            bool res = store->candidate_get(a2,info,txn);
            ASSERT_TRUE(res);
            eb.previous = eb.Hash();
            ASSERT_FALSE(store->epoch_put(eb,txn));
            ASSERT_FALSE(store->epoch_tip_put(eb.CreateTip(),txn));
            RepInfo rep;
            RenounceCandidacy req;
            req.origin = a2;
            rep.candidacy_action_tip = req.Hash();
            store->request_put(req,txn);
            store->rep_put(a2,rep,txn);
        }
    }

    mgr.AddReelectionCandidates(0,txn);

    {
        CandidateInfo info;
        init_ecies(info.ecies_key);
        bool res = store->candidate_get(a2,info,txn);
        ASSERT_TRUE(res);
    }

    AnnounceCandidacy req;
    init_ecies(req.ecies_key);
    req.origin = a2;
    RepInfo rep;
    rep.candidacy_action_tip = req.Hash();
    store->request_put(req,txn);
    store->rep_put(a2,rep,txn);

    mgr.AddReelectionCandidates(0,txn);
    {
        CandidateInfo info;
        init_ecies(info.ecies_key);
        bool res = store->candidate_get(a2,info,txn);
        ASSERT_FALSE(res);
    }
}

TEST(Elections,get_next_epoch_delegates)
{
    logos::block_store* store = get_db();
    clear_dbs();
    DelegateIdentityManager::EpochTransitionEnable(true);

    EpochVotingManager::ENABLE_ELECTIONS = true;

    uint32_t epoch_num = 1;
    auto block = create_eb_preprepare(false);
    AggSignature sig;
    ApprovedEB eb(block, sig, sig);
    eb.epoch_number = epoch_num-1;
    eb.previous = 0;
    EpochVotingManager voting_mgr(*store);
    PersistenceManager<ECT> persistence_mgr(*store,nullptr);
    std::vector<Delegate> delegates;
    //This is set large so that way every delegate stays under the cap
    //and votes are not redistributed
    auto base_vote = 100000;
    for(size_t i = 0; i < 32; ++i)
    {

        logos::transaction txn(store->environment, nullptr, true);

        Delegate d(init_delegate(i,base_vote+i,i==0?1:i,i));
        d.starting_term = true;
        eb.delegates[i] = d;
        delegates.push_back(d);

        RepInfo rep;

        AnnounceCandidacy announce;
        init_ecies(announce.ecies_key);
        announce.origin = i;
        announce.bls_key = d.bls_pub;
        announce.stake = i == 0 ? 1 : i;
        rep.candidacy_action_tip = announce.Hash();
        store->request_put(announce,txn);
        VotingPowerManager::GetInstance()->AddSelfStake(i,i==0?1:i,epoch_num,txn);
        
        StartRepresenting start_rep;
        start_rep.origin = i;
        rep.rep_action_tip = start_rep.Hash();
        store->request_put(start_rep,txn);
        
        store->rep_put(i,rep,txn);
    }

    std::reverse(delegates.begin(),delegates.end());
    std::reverse(std::begin(eb.delegates),std::end(eb.delegates));
    {
        logos::transaction txn(store->environment,nullptr,true);
        ASSERT_FALSE(store->epoch_tip_put(eb.CreateTip(),txn));
        ASSERT_FALSE(store->epoch_put(eb,txn));
    }

    EpochVotingManager::START_ELECTIONS_EPOCH = 4;

    auto transition_epoch = [&](int retire_idx = -1)
    {
        ++epoch_num;
        std::cout << "transitioning to epoch number " << epoch_num << std::endl;
        eb.previous = eb.Hash();
        eb.epoch_number = epoch_num-1;
        logos::transaction txn(store->environment,nullptr,true);
        eb.is_extension = !voting_mgr.GetNextEpochDelegates(eb.delegates,epoch_num);
        ASSERT_FALSE(store->epoch_tip_put(eb.CreateTip(),txn));
        ASSERT_FALSE(store->epoch_put(eb,txn));
        persistence_mgr.TransitionCandidatesDBNextEpoch(txn, epoch_num);

    };

    auto compare_delegates = [&]()
    {

        logos::transaction txn(store->environment,nullptr,true);
        for(size_t i = 0; i < 32; ++i)
        {
            ASSERT_EQ(eb.delegates[i].account,delegates[i].account);
            if(eb.delegates[i].stake != delegates[i].stake)
            {
                VotingPowerInfo vp_info;
                VotingPowerManager::GetInstance()->GetVotingPowerInfo(
                        delegates[i].account,
                        eb.epoch_number+1,
                        vp_info,
                        txn);

                std::cout << "epoch num = " << eb.epoch_number+1
                    << " i = " << i 
                    << " delegate stake = " << delegates[i].stake.number()
                    << " eb delegate stake = " << eb.delegates[i].stake.number()
                    << " voting power mgr stake = " 
                     << vp_info.current.self_stake.number()
                    << std::endl;
                trace_and_halt();
            }

            ASSERT_EQ(eb.delegates[i].stake,delegates[i].stake);

            ASSERT_EQ(eb.delegates[i].bls_pub,delegates[i].bls_pub);

            ASSERT_EQ(eb.delegates[i].vote,delegates[i].vote);

            ASSERT_EQ(eb.delegates[i].starting_term,delegates[i].starting_term);
            ASSERT_EQ(eb.delegates[i],delegates[i]);
        }
    };

    auto get_candidates = [&store]() -> std::vector<CandidateInfo>
    {
        std::vector<CandidateInfo> results;
        logos::transaction txn(store->environment,nullptr,false);
        for(auto it = logos::store_iterator(txn, store->candidacy_db);
                it != logos::store_iterator(nullptr); ++it)
        {
            bool error = false;
            CandidateInfo info(error,it->second);
            init_ecies(info.ecies_key);
            assert(!error);
            results.push_back(info);
        }
        return results;
    };

    compare_delegates();

    transition_epoch();

    for(size_t i = 0; i < 32; ++i)
    {
        delegates[i].starting_term = false;
    }


    compare_delegates();

    transition_epoch();

    compare_delegates();

    transition_epoch();

    compare_delegates();

    
    auto candidates = get_candidates();

    ASSERT_EQ(candidates.size(),delegates.size());

    {
        logos::transaction txn(store->environment,nullptr,true);
        for(size_t i = 0; i < 8; ++i)
        {
            auto new_vote = delegates[i].vote+100;
            store->candidate_add_vote(delegates[i].account,new_vote,epoch_num,txn);
            delegates[i].raw_vote = new_vote;
            delegates[i].vote = new_vote;
            delegates[i].starting_term = true;
        }
        std::sort(delegates.begin(),delegates.end(),[](auto d1, auto d2){
                return d1.vote > d2.vote;
                });
    }
    transition_epoch(0);
    compare_delegates();
    candidates = get_candidates();
    ASSERT_EQ(candidates.size(),24);
    {
        logos::transaction txn(store->environment,nullptr,true);
        for(size_t i = 8; i < 16; ++i)
        {
            auto new_vote = delegates[i].vote+200;
            store->candidate_add_vote(delegates[i].account,new_vote,epoch_num,txn);
            delegates[i].raw_vote = new_vote;
            delegates[i].vote = new_vote; 
            delegates[i].starting_term = true;
        }
        for(size_t i = 0; i < 8; ++i)
        {
            delegates[i].starting_term = false;
        }
        std::sort(delegates.begin(),delegates.end(),[](auto d1, auto d2){
                return d1.vote > d2.vote;
                });
    }
    transition_epoch(0);
    compare_delegates();
    candidates = get_candidates();
    ASSERT_EQ(candidates.size(),16);

    {
        logos::transaction txn(store->environment,nullptr,true);
        for(size_t i = 16; i < 24; ++i)
        {
            auto new_vote = delegates[i].vote+300;
            store->candidate_add_vote(delegates[i].account,new_vote,epoch_num,txn);
            delegates[i].raw_vote = new_vote;
            delegates[i].vote = new_vote; 
            delegates[i].starting_term = true;
        }
        for(size_t i = 0; i < 8; ++i)
        {
            delegates[i].starting_term = false;
        }
        std::sort(delegates.begin(),delegates.end(),[](auto d1, auto d2){
                return d1.vote > d2.vote;
                });
    }

    transition_epoch(0);
    compare_delegates();
    candidates = get_candidates();
    ASSERT_EQ(candidates.size(),8);


    {
        logos::transaction txn(store->environment,nullptr,true);
        for(size_t i = 24; i < 32; ++i)
        {
            auto new_vote = delegates[i].vote+400;
            store->candidate_add_vote(delegates[i].account,new_vote,epoch_num,txn);
            delegates[i].raw_vote = new_vote;
            delegates[i].vote = new_vote; 
            delegates[i].starting_term = true;
        }
        for(size_t i = 0; i < 8; ++i)
        {
            delegates[i].starting_term = false;
        }
        std::sort(delegates.begin(),delegates.end(),[](auto d1, auto d2){
                return d1.vote > d2.vote;
                });
    }

    transition_epoch(0);
    compare_delegates();



    std::cout << "starting long loop ********************"
        << "epoch_num = " << epoch_num << std::endl;

    for(size_t e = 0; e < 50; ++e)
    {
    candidates = get_candidates();
    ASSERT_EQ(candidates.size(),8);
    ASSERT_EQ(voting_mgr.GetRetiringDelegates(epoch_num+1).size(),8);
        {
            logos::transaction txn(store->environment,nullptr,true);
            for(size_t i = 24; i < 32; ++i)
            {
                auto new_vote = delegates[i].vote + 500;
                ASSERT_FALSE(store->candidate_add_vote(delegates[i].account,new_vote,epoch_num,txn));
                delegates[i].raw_vote = new_vote;
                delegates[i].vote = new_vote; 
                delegates[i].starting_term = true;
            }
            for(size_t i = 0; i < 8; ++i)
            {
                delegates[i].starting_term = false;
            }
            std::sort(delegates.begin(),delegates.end(),[](auto d1, auto d2){
                    return d1.vote > d2.vote;
                    });
        }
    transition_epoch();
    compare_delegates();
    }
    
    std::cout << "finished normal case ****************" << std::endl;


    //Test extension of delegate term

    ASSERT_FALSE(eb.is_extension);
    std::unordered_set<Delegate> retiring = voting_mgr.GetRetiringDelegates(epoch_num+1);
    ApprovedEB retiring_eb(block, sig, sig);
    store->epoch_get_n(3, retiring_eb,nullptr,[](ApprovedEB& block) { return !block.is_extension;});
    transition_epoch();
    ASSERT_TRUE(eb.is_extension);

    ApprovedEB eb2(block, sig, sig);
    store->epoch_get_n(0, eb2);
    ASSERT_TRUE(eb2.is_extension);
    for(size_t i = 0; i < NUM_DELEGATES; ++i)
    {
        delegates[i].starting_term = false;
    }

    ApprovedEB retiring_eb2(block, sig, sig);
    store->epoch_get_n(3, retiring_eb2,nullptr,[](ApprovedEB& block) { return !block.is_extension;});
    ASSERT_EQ(retiring_eb.epoch_number,retiring_eb2.epoch_number);

    compare_delegates();

    ASSERT_EQ(voting_mgr.GetRetiringDelegates(epoch_num+1),retiring);
    transition_epoch();
    ASSERT_TRUE(eb.is_extension);
    ASSERT_EQ(voting_mgr.GetRetiringDelegates(epoch_num+1), retiring);
    compare_delegates();


    //not enough votes
    for(size_t i = 24; i <28 ; ++i)
    {
        logos::transaction txn(store->environment,nullptr,true);
        ASSERT_FALSE(store->candidate_add_vote(delegates[i].account,delegates[i].vote+500,epoch_num,txn));
    }

    transition_epoch();
    ASSERT_TRUE(eb.is_extension);
    ASSERT_EQ(voting_mgr.GetRetiringDelegates(epoch_num+1), retiring);


    for(size_t i = 24; i < 32; ++i)
    {
        logos::transaction txn(store->environment,nullptr,true);
        ASSERT_FALSE(store->candidate_add_vote(delegates[i].account,delegates[i].vote+500,epoch_num,txn));
        delegates[i].raw_vote += 500;
        delegates[i].vote += 500;
        delegates[i].starting_term = true;
    }
    std::sort(delegates.begin(), delegates.end(),[](auto d1, auto d2)
            {
                return d1.vote > d2.vote;
            });
    transition_epoch();
    ASSERT_FALSE(eb.is_extension);
    compare_delegates();

    //make sure proper candidates were added for reelection
    for(size_t i = 24; i < 32; ++i)
    {
        logos::transaction txn(store->environment,nullptr,true);
        ASSERT_FALSE(store->candidate_add_vote(delegates[i].account,delegates[i].vote+500,epoch_num,txn));
        delegates[i].raw_vote += 500;
        delegates[i].vote += 500;
        delegates[i].starting_term = true;
    }



    EpochVotingManager::ENABLE_ELECTIONS = false;
}

TEST(Elections, redistribute_votes)
{

    logos::block_store* store = get_db();

    EpochVotingManager mgr(*store);
    Delegate delegates[32];

    auto sum = 0;
    for(size_t i = 0; i < 32; ++i)
    {
        Delegate d(init_delegate(i,i,i,i));
        delegates[i] = d;
        sum += i;
    }
    auto cap = sum / 8;
    std::sort(std::begin(delegates),std::end(delegates),
            [](auto d1, auto d2)
            {
            return d1.vote > d2.vote;
            });
    mgr.Redistribute(delegates, &Delegate::vote);

    for(size_t i = 0; i < 32; ++i)
    {
        ASSERT_EQ(delegates[i].account,32-i-1);
        ASSERT_LE(delegates[i].vote.number(), cap);
        if(i != 31)
        {
            ASSERT_GE(delegates[i].vote.number(),delegates[i+1].vote.number());
        }
    }
    sum = 0;
    for(size_t i = 0; i < 32; ++i)
    {

        delegates[i].vote = (i == 0 ? 6369 : 1); 
        sum += (i == 0 ? 6369 : 1);
    }
    cap = sum / 8;
    ASSERT_EQ(sum, 6400);
    ASSERT_EQ(cap, 800);

    mgr.Redistribute(delegates, &Delegate::vote);

    for(size_t i = 0; i < 32; ++i)
    {
        ASSERT_LE(delegates[i].vote.number(),cap);
        ASSERT_EQ(delegates[i].vote.number(),i == 0 ? cap : 180);
    }

    sum = 0;
    for(size_t i = 0; i < 32; ++i)
    {
        delegates[i].vote = (i == 0 || i == 1 ? 1000 : 1);
        sum += (i == 0 || i == 1 ? 1000 : 1);
    }
    cap = sum / 8;

    ASSERT_EQ(sum,2030);
    ASSERT_EQ(cap,253);
    mgr.Redistribute(delegates, &Delegate::vote);
    for(size_t i = 0; i < 32; ++i)
    {
        ASSERT_LE(delegates[i].vote.number(),cap);
        ASSERT_EQ(delegates[i].vote.number(),i == 0 || i == 1 ? cap : 50); 
    }
}

TEST(Elections, is_dead_period)
{
    logos::block_store* store = get_db();
    clear_dbs();
    PersistenceManager<R> persistence_mgr(*store,nullptr);
    PersistenceManager<ECT> epoch_persistence_mgr(*store,nullptr);
    logos::transaction txn(store->environment,nullptr,true);

    uint32_t epoch_num = 1;
    auto block = create_eb_preprepare(false);
    AggSignature sig;
    ApprovedEB eb(block, sig, sig);
    eb.epoch_number = epoch_num-1;
    eb.previous = 0;

    ASSERT_FALSE(store->epoch_tip_put(eb.CreateTip(),txn));
    ASSERT_FALSE(store->epoch_put(eb,txn));

    ASSERT_FALSE(persistence_mgr.IsDeadPeriod(epoch_num, txn));
    ASSERT_TRUE(persistence_mgr.IsDeadPeriod(epoch_num+1, txn));
}

TEST(Elections,validate)
{
    logos::block_store* store = get_db();
    clear_dbs();
    PersistenceManager<R> persistence_mgr(*store,nullptr);
    PersistenceManager<ECT> epoch_persistence_mgr(*store,nullptr);
    logos::transaction txn(store->environment,nullptr,true);

    logos::process_return result;
    result.code = logos::process_result::progress; 
    DelegateIdentityManager::EpochTransitionEnable(true);
    AccountAddress sender_account = 100;
    AccountAddress sender_account2 = 101;
    logos::account_info account_info(0,0,0,0,MIN_DELEGATE_STAKE+MIN_DELEGATE_STAKE,0,0,0,0);

    store->account_put(sender_account, account_info, txn);
    store->account_put(sender_account2, account_info, txn);
    EpochVotingManager::START_ELECTIONS_EPOCH = 2;
    EpochVotingManager::ENABLE_ELECTIONS = true;

    uint32_t epoch_num = 1;
    ElectionVote vote;
    vote.origin = sender_account;
    vote.epoch_num = epoch_num;
    AnnounceCandidacy announce;
    init_ecies(announce.ecies_key);
    announce.origin = sender_account;
    announce.set_stake = true;
    announce.stake = MIN_DELEGATE_STAKE;
    announce.epoch_num = epoch_num;
    RenounceCandidacy renounce;
    renounce.origin = sender_account;
    renounce.epoch_num = epoch_num;
    StartRepresenting start_rep;
    start_rep.origin = sender_account;
    start_rep.set_stake = true;
    start_rep.stake = MIN_DELEGATE_STAKE;
    start_rep.epoch_num = epoch_num;
    StopRepresenting stop_rep;
    stop_rep.origin = sender_account;
    stop_rep.epoch_num = epoch_num;
    announce.Hash();
    renounce.Hash();
    start_rep.Hash();
    stop_rep.Hash();
    vote.Hash();

    auto block = create_eb_preprepare(false);
    AggSignature sig;
    ApprovedEB eb(block, sig, sig);
    eb.epoch_number = epoch_num-1;
    eb.previous = 0;


    auto update_governance_subchain = [&]() {
        vote.governance_subchain_prev = account_info.governance_subchain_head;
        announce.governance_subchain_prev = account_info.governance_subchain_head;
        renounce.governance_subchain_prev = account_info.governance_subchain_head;
        start_rep.governance_subchain_prev = account_info.governance_subchain_head;
        stop_rep.governance_subchain_prev = account_info.governance_subchain_head;
        vote.Hash();
        announce.Hash();
        renounce.Hash();
        start_rep.Hash();
        stop_rep.Hash();
    
    };

    for(size_t i = 0; i < 32; ++i)
    {
        Delegate d(init_delegate(i,i,i,i));
        d.starting_term = false; // this is not really neccessary in genesis
        eb.delegates[i] = d;

        RepInfo rep;
        store->rep_put(i,rep,txn);
    }
    std::reverse(std::begin(eb.delegates),std::end(eb.delegates));
    ASSERT_FALSE(store->epoch_tip_put(eb.CreateTip(),txn));
    ASSERT_FALSE(store->epoch_put(eb,txn));

    //epoch block created, but only StartRepresenting and AnnounceCandidacy should pass
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));

    persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result);
    std::cout << "result.code = " << ProcessResultToString(result.code) << std::endl;
    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(start_rep,account_info,epoch_num,txn,result));

    persistence_mgr.ApplyRequest(start_rep,account_info,txn);
    update_governance_subchain();


    VotingPowerInfo vp_info;
    ASSERT_TRUE(VotingPowerManager::GetInstance()->GetVotingPowerInfo(start_rep.origin, vp_info,txn));
    ASSERT_EQ(vp_info.next.self_stake,start_rep.stake);
    ASSERT_EQ(vp_info.next.unlocked_proxied,0);


    //all should fail
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,account_info,epoch_num,txn,result));

    
    auto transition_epoch = [&](std::vector<AccountAddress> new_delegates = {})
    {
        ++epoch_num;
        std::cout << "transition to epoch_num " << epoch_num << std::endl;
        eb.previous = eb.Hash();
        eb.epoch_number = epoch_num-1;
        vote.epoch_num = epoch_num;
        announce.epoch_num = epoch_num;
        renounce.epoch_num = epoch_num;
        start_rep.epoch_num = epoch_num;
        stop_rep.epoch_num = epoch_num;

        vote.governance_subchain_prev = account_info.governance_subchain_head;
        announce.governance_subchain_prev = account_info.governance_subchain_head;
        renounce.governance_subchain_prev = account_info.governance_subchain_head;
        start_rep.governance_subchain_prev = account_info.governance_subchain_head;
        stop_rep.governance_subchain_prev = account_info.governance_subchain_head;
        vote.Hash();
        announce.Hash();
        renounce.Hash();
        start_rep.Hash();
        stop_rep.Hash();
        bool reelection = epoch_num > 3;
        for(Delegate& del : eb.delegates)
        {
            del.starting_term = false;
        }
        for(size_t i = 0; i < new_delegates.size(); ++i)
        {
            eb.delegates[i].account = new_delegates[i];
            eb.delegates[i].starting_term = true;
        }
        ASSERT_FALSE(store->epoch_tip_put(eb.CreateTip(),txn));
        ASSERT_FALSE(store->epoch_put(eb,txn));
        epoch_persistence_mgr.TransitionNextEpoch(txn,epoch_num > 3 ? epoch_num : 0);
    };


    auto get_candidates = [&store,&txn]() -> std::vector<CandidateInfo>
    {
        std::vector<CandidateInfo> results;
        for(auto it = logos::store_iterator(txn, store->candidacy_db);
                it != logos::store_iterator(nullptr); ++it)
        {
            bool error = false;
            CandidateInfo info(error,it->second);
            init_ecies(info.ecies_key);
            assert(!error);
            results.push_back(info);
        }
        return results;
    };

    transition_epoch();

    ASSERT_TRUE(VotingPowerManager::GetInstance()->GetVotingPowerInfo(start_rep.origin, vp_info,txn));
    ASSERT_EQ(vp_info.next.self_stake,start_rep.stake);

    //active rep
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));

    bool res = persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result);
    if(!res)
    {
        std::cout << ProcessResultToString(result.code) << std::endl;
        ASSERT_TRUE(false);
    }
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,account_info,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,account_info,epoch_num,txn,result));


    //cast a vote
    persistence_mgr.ApplyRequest(vote,account_info,txn);
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    persistence_mgr.ApplyRequest(announce,account_info,txn);
    update_governance_subchain();
    ASSERT_TRUE(VotingPowerManager::GetInstance()->GetVotingPowerInfo(start_rep.origin, vp_info,txn));
    ASSERT_EQ(vp_info.next.self_stake,announce.stake);
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,account_info,epoch_num,txn,result));

    //added as candidate
    auto candidates = get_candidates();
    ASSERT_EQ(candidates.size(),1);
    transition_epoch();

    ASSERT_TRUE(VotingPowerManager::GetInstance()->GetVotingPowerInfo(start_rep.origin, vp_info,txn));
    ASSERT_EQ(vp_info.next.self_stake,start_rep.stake);

    ASSERT_EQ(get_candidates().size(),1);

    //active candidate
    CandidateInfo info;
    init_ecies(info.ecies_key);
    ASSERT_FALSE(store->candidate_get(announce.origin,info,txn));
    ASSERT_EQ(info.votes_received_weighted,0);

    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(renounce,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result));

    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));

    persistence_mgr.ApplyRequest(renounce,account_info,txn);
    update_governance_subchain();
    ASSERT_TRUE(VotingPowerManager::GetInstance()->GetVotingPowerInfo(start_rep.origin, vp_info,txn));
    ASSERT_EQ(vp_info.next.self_stake,announce.stake);

    ASSERT_EQ(get_candidates().size(),1);

    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));


    //renounced but still can receive votes
    vote.votes.emplace_back(sender_account,8);
    vote.Hash();
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    persistence_mgr.ApplyRequest(vote,account_info,txn);

    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));

    transition_epoch();

    //no longer candidate
    ASSERT_EQ(get_candidates().size(),0);
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    vote.votes.clear();
    vote.Hash();

    //only a rep again
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,account_info,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,account_info,epoch_num,txn,result));

    persistence_mgr.ApplyRequest(stop_rep,account_info,txn);
    update_governance_subchain();
    ASSERT_TRUE(VotingPowerManager::GetInstance()->GetVotingPowerInfo(start_rep.origin, vp_info,txn));
    ASSERT_EQ(vp_info.next.self_stake,start_rep.stake);
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,account_info,epoch_num,txn,result));



    transition_epoch();
    ASSERT_TRUE(VotingPowerManager::GetInstance()->GetVotingPowerInfo(start_rep.origin, vp_info,txn));
    ASSERT_EQ(vp_info.next.self_stake,announce.stake);
    //no longer rep
    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(start_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,account_info,epoch_num,txn,result));

    //announce will also auto add account as rep
    persistence_mgr.ApplyRequest(announce,account_info,txn);
    update_governance_subchain();
    ASSERT_TRUE(VotingPowerManager::GetInstance()->GetVotingPowerInfo(start_rep.origin, vp_info,txn));
    ASSERT_EQ(vp_info.next.self_stake,announce.stake);

    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,account_info,epoch_num,txn,result));

    transition_epoch();

    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    vote.votes.emplace_back(announce.origin,8);
    vote.Hash();
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(renounce,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));

    persistence_mgr.ApplyRequest(renounce,account_info,txn);
    update_governance_subchain();
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));

    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    transition_epoch();


    ASSERT_TRUE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));

    persistence_mgr.ApplyRequest(stop_rep,account_info,txn);
    update_governance_subchain();
    ASSERT_TRUE(VotingPowerManager::GetInstance()->GetVotingPowerInfo(start_rep.origin, vp_info,txn));
    ASSERT_EQ(vp_info.next.self_stake,announce.stake);

    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));

    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,account_info,epoch_num,txn,result));

    transition_epoch();

    ASSERT_TRUE(persistence_mgr.ValidateRequest(start_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,account_info,epoch_num,txn,result));


    persistence_mgr.ApplyRequest(start_rep,account_info,txn);
    update_governance_subchain();
    ASSERT_TRUE(VotingPowerManager::GetInstance()->GetVotingPowerInfo(start_rep.origin, vp_info,txn));
    ASSERT_EQ(vp_info.next.self_stake,announce.stake);

    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,account_info,epoch_num,txn,result));

    transition_epoch();

    vote.votes.clear();
    vote.Hash();
    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,account_info,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,account_info,epoch_num,txn,result));

    persistence_mgr.ApplyRequest(announce,account_info,txn);
    update_governance_subchain();
    ASSERT_TRUE(VotingPowerManager::GetInstance()->GetVotingPowerInfo(announce.origin, vp_info,txn));

    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,account_info,epoch_num,txn,result));


    transition_epoch();

    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(renounce,account_info,epoch_num,txn,result));


    //add account to delegate set
    std::vector<AccountAddress> new_delegates;
    new_delegates.push_back(announce.origin);
    transition_epoch(new_delegates);
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result));

    vote.votes.emplace_back(announce.origin,8);
    vote.Hash();
    //account no longer candidate because account is delegate-elect
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    vote.votes.clear();
    vote.Hash();

    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(renounce,account_info,epoch_num,txn,result));

    persistence_mgr.ApplyRequest(renounce,account_info,txn);
    update_governance_subchain();

    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,account_info,epoch_num,txn,result));

    transition_epoch();

    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,account_info,epoch_num,txn,result));

    transition_epoch();

    transition_epoch();

    //verify account is not added for reelection
    vote.votes.emplace_back(announce.origin,8);
    vote.Hash();
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));

    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,account_info,epoch_num,txn,result));
    persistence_mgr.ApplyRequest(announce,account_info,txn);
    update_governance_subchain();
        
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    transition_epoch();
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));

    //add account to delegate set again
    transition_epoch({announce.origin});
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    transition_epoch();

    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    transition_epoch();
    
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    transition_epoch();

    //verify account was added for reelection
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,account_info,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,account_info,epoch_num,txn,result));


    EpochVotingManager::ENABLE_ELECTIONS = false;
}

TEST(Elections, apply)
{

    logos::block_store* store = get_db();
    clear_dbs();
    DelegateIdentityManager::EpochTransitionEnable(true);

    EpochVotingManager::ENABLE_ELECTIONS = true;

    uint32_t epoch_num = 1;
    auto block = create_eb_preprepare(false);
    AggSignature sig;
    ApprovedEB eb(block, sig, sig);
    eb.epoch_number = epoch_num-1;
    eb.previous = 0;
    EpochVotingManager voting_mgr(*store);
    PersistenceManager<ECT> epoch_persistence_mgr(*store,nullptr);

    PersistenceManager<R> req_persistence_mgr(*store,nullptr);
    std::vector<Delegate> delegates;
    //This is set large so that way every delegate stays under the cap
    //and votes are not redistributed
    auto base_vote = 100000;
    for(size_t i = 0; i < 32; ++i)
    {

        logos::transaction txn(store->environment, nullptr, true);

        Amount stake = i == 0 ? 1 : i;
        Delegate d(init_delegate(i,base_vote+i,stake,false));
        d.starting_term = true;
        eb.delegates[i] = d;
        delegates.push_back(d);

        RepInfo rep;

        AnnounceCandidacy announce;
        init_ecies(announce.ecies_key);
        announce.origin = i;
        announce.set_stake = true;
        announce.stake = stake;
        announce.bls_key = i;
        rep.candidacy_action_tip = announce.Hash();
        store->request_put(announce,txn);
        
        StartRepresenting start_rep;
        start_rep.origin = i;
        start_rep.set_stake = true;
        start_rep.stake = MIN_DELEGATE_STAKE;
        rep.rep_action_tip = start_rep.Hash();
        store->request_put(start_rep,txn);
        
        store->rep_put(i,rep,txn);


        VotingPowerManager::GetInstance()->AddSelfStake(announce.origin,announce.stake,epoch_num,txn);
    }

    std::reverse(delegates.begin(),delegates.end());
    std::reverse(std::begin(eb.delegates),std::end(eb.delegates));
    {
        logos::transaction txn(store->environment,nullptr,true);
        ASSERT_FALSE(store->epoch_tip_put(eb.CreateTip(),txn));
        ASSERT_FALSE(store->epoch_put(eb,txn));
    }

    EpochVotingManager::START_ELECTIONS_EPOCH = 4;

    init_tips(epoch_num);


    auto transition_epoch = [&]()
    {
        init_tips(epoch_num);
        ++epoch_num;
        eb.previous = eb.Hash();
        eb.epoch_number = epoch_num-1;
        voting_mgr.GetNextEpochDelegates(eb.delegates,epoch_num);
        epoch_persistence_mgr.ApplyUpdates(eb);
    };

    auto get_candidates = [&store]() -> std::vector<CandidateInfo>
    {

        logos::transaction txn(store->environment,nullptr,true);
        std::vector<CandidateInfo> results;
        for(auto it = logos::store_iterator(txn, store->candidacy_db);
                it != logos::store_iterator(nullptr); ++it)
        {
            bool error = false;
            CandidateInfo info(error,it->second);
            init_ecies(info.ecies_key);
            assert(!error);
            results.push_back(info);
        }
        return results;
    };


    std::vector<AccountAddress> reps;

    for(size_t i = 0; i < 16; ++i)
    {
        logos::transaction txn(store->environment, nullptr, true);
        StartRepresenting start_rep;
        start_rep.origin = 100+i;
        start_rep.set_stake = true;
        start_rep.stake = MIN_DELEGATE_STAKE + 10 + 10 * (i%2);
        start_rep.epoch_num = epoch_num;
        start_rep.Hash();

        logos::account_info account_info(0,0,0,0,MIN_DELEGATE_STAKE+1000,0,0,0,0);
        store->account_put(start_rep.origin, account_info, txn);
        logos::process_return result;
        ASSERT_TRUE(req_persistence_mgr.ValidateRequest(start_rep,account_info, epoch_num, txn, result));
        req_persistence_mgr.ApplyRequest(start_rep,account_info,txn);
        reps.push_back(start_rep.origin);
    }

    ASSERT_EQ(get_candidates().size(),0);
    transition_epoch();
    transition_epoch();

    for(auto account : reps)
    {
        if(account.number() < (100+8))
        {
            logos::transaction txn(store->environment, nullptr, true);
            AnnounceCandidacy announce;
            init_ecies(announce.ecies_key);
            announce.origin = account;
            announce.epoch_num = epoch_num;
            announce.set_stake = false;
            announce.Hash();
            logos::account_info account_info;
            store->account_get(account, account_info, txn);

            logos::process_return result;

            req_persistence_mgr.ValidateRequest(announce,account_info, epoch_num, txn, result);
            std::cout << "result.code = " << ProcessResultToString(result.code) << std::endl;
            ASSERT_TRUE(req_persistence_mgr.ValidateRequest(announce,account_info, epoch_num, txn, result));
            req_persistence_mgr.ApplyRequest(announce, account_info, txn);
        }
    }

    transition_epoch();

    ASSERT_EQ(get_candidates().size(),40);

    {
        logos::transaction txn(store->environment, nullptr, true);

        logos::process_return result;
        logos::account_info account_info;

        ElectionVote ev;
        ev.origin = reps[0];
        ev.votes.emplace_back(eb.delegates[0].account,8);
        ev.epoch_num = epoch_num;
        ev.Hash();
        ASSERT_FALSE(store->account_get(reps[0], account_info, txn));
        ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, account_info, txn, result));
        req_persistence_mgr.ApplyRequest(ev, account_info, txn);

        ev.votes.clear();
        ev.origin = reps[1];
        ev.votes.emplace_back(eb.delegates[1].account,8);
        ev.Hash();
        ASSERT_FALSE(store->account_get(reps[1], account_info, txn));
        ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, account_info,txn, result));
        req_persistence_mgr.ApplyRequest(ev, account_info, txn);

        ev.votes.clear();
        ev.origin = reps[2];
        ev.votes.emplace_back(eb.delegates[2].account,8);
        ev.Hash();
        ASSERT_FALSE(store->account_get(reps[2], account_info, txn));
        ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, account_info, txn, result));
        req_persistence_mgr.ApplyRequest(ev, account_info, txn);

        ev.votes.clear();
        ev.origin = reps[3];
        ev.votes.emplace_back(eb.delegates[3].account,8);
        ev.Hash();
        ASSERT_FALSE(store->account_get(reps[3], account_info, txn));
        ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, account_info, txn, result));
        req_persistence_mgr.ApplyRequest(ev, account_info, txn);

        ev.votes.clear();
        ev.origin = reps[4];
        ev.votes.emplace_back(reps[0],8);
        ev.Hash();
        ASSERT_FALSE(store->account_get(reps[4], account_info, txn));
        ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, account_info, txn, result));
        req_persistence_mgr.ApplyRequest(ev, account_info, txn);

        ev.votes.clear();
        ev.origin = reps[5];
        ev.votes.emplace_back(reps[1],8);
        ev.Hash();
        ASSERT_FALSE(store->account_get(reps[5], account_info, txn));
        ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, account_info, txn, result));
        req_persistence_mgr.ApplyRequest(ev, account_info, txn);

        ev.votes.clear();
        ev.origin = reps[6];
        ev.votes.emplace_back(reps[2],8);
        ev.Hash();
        ASSERT_FALSE(store->account_get(reps[6], account_info, txn));
        ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, account_info, txn, result));
        req_persistence_mgr.ApplyRequest(ev, account_info, txn);

        ev.votes.clear();
        ev.origin = reps[7];
        ev.votes.emplace_back(reps[3],8);
        ev.Hash();
        ASSERT_FALSE(store->account_get(reps[7], account_info, txn));
        ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, account_info, txn, result));
        req_persistence_mgr.ApplyRequest(ev, account_info, txn);

        ev.votes.clear();
        ev.origin = reps[8];
        ev.votes.emplace_back(reps[4],8);
        ev.Hash();
        ASSERT_FALSE(store->account_get(reps[8], account_info, txn));
        ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, account_info, txn, result));
        req_persistence_mgr.ApplyRequest(ev, account_info, txn);

        ev.votes.clear();
        ev.origin = reps[9];
        ev.votes.emplace_back(reps[4],8);
        ev.Hash();
        ASSERT_FALSE(store->account_get(reps[9], account_info, txn));
        ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, account_info, txn, result));
        req_persistence_mgr.ApplyRequest(ev, account_info, txn);

        ev.votes.clear();
        ev.origin = reps[10];
        ev.votes.emplace_back(eb.delegates[4].account,8);
        ev.Hash();
        ASSERT_FALSE(store->account_get(reps[10], account_info, txn));
        ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, account_info, txn, result));
        req_persistence_mgr.ApplyRequest(ev, account_info, txn);

        ev.votes.clear();
        ev.origin = reps[11];
        ev.votes.emplace_back(eb.delegates[4].account,8);
        ev.Hash();
        ASSERT_FALSE(store->account_get(reps[11], account_info, txn));
        ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, account_info, txn, result));
        req_persistence_mgr.ApplyRequest(ev, account_info, txn);

        ev.votes.clear();
        ev.origin = reps[12];
        ev.votes.emplace_back(reps[0],8);
        ev.Hash();
        ASSERT_FALSE(store->account_get(reps[12], account_info, txn));
        ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, account_info, txn, result));
        req_persistence_mgr.ApplyRequest(ev, account_info, txn);

        ev.votes.clear();
        ev.origin = reps[13];
        ev.votes.emplace_back(reps[0],8);
        ev.Hash();
        ASSERT_FALSE(store->account_get(reps[13], account_info, txn));
        ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, account_info, txn, result));
        req_persistence_mgr.ApplyRequest(ev, account_info, txn);

        ev.votes.clear();
        ev.origin = reps[14];
        ev.votes.emplace_back(eb.delegates[0].account,8);
        ev.Hash();
        ASSERT_FALSE(store->account_get(reps[14], account_info, txn));
        ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, account_info, txn, result));
        req_persistence_mgr.ApplyRequest(ev, account_info, txn);

        ev.votes.clear();
        ev.origin = reps[15];
        ev.votes.emplace_back(eb.delegates[0].account,8);
        ev.Hash();
        ASSERT_FALSE(store->account_get(reps[15], account_info, txn));
        ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, account_info, txn, result));
        req_persistence_mgr.ApplyRequest(ev, account_info, txn);
    }


    std::unordered_map<AccountAddress,Amount> election_results;
    {
        logos::transaction txn(store->environment,nullptr,true);
        std::vector<CandidateInfo> results;
        for(auto it = logos::store_iterator(txn, store->candidacy_db);
                it != logos::store_iterator(nullptr); ++it)
        {
            bool error = false;
            CandidateInfo info(error,it->second);
            init_ecies(info.ecies_key);
            assert(!error);
            election_results[it->first.uint256()] = info.votes_received_weighted;
        }
    }

    Amount base_vote_weight = MIN_DELEGATE_STAKE * 8;

    ASSERT_EQ(election_results[29],base_vote_weight+80);
    ASSERT_EQ(election_results[31],base_vote_weight*3+320);
    ASSERT_EQ(election_results[30],base_vote_weight+160);
    ASSERT_EQ(election_results[28],base_vote_weight+160);
    ASSERT_EQ(election_results[27],base_vote_weight*2+240);
    ASSERT_EQ(election_results[reps[0]],base_vote_weight*3+320);
    ASSERT_EQ(election_results[reps[1]],base_vote_weight+160);
    ASSERT_EQ(election_results[reps[2]],base_vote_weight+80);
    ASSERT_EQ(election_results[reps[3]],base_vote_weight+160);
    ASSERT_EQ(election_results[reps[4]],base_vote_weight*2+240);


    auto winners = voting_mgr.GetElectionWinners(8);

    auto winners_contains = [&winners](auto account)
    {
        for(auto winner : winners)
        {
            if(winner.first == account)
            {
                return true;
            }

        }
        return false;
    };

    ASSERT_TRUE(winners_contains(31));
    ASSERT_TRUE(winners_contains(30));
    ASSERT_FALSE(winners_contains(29));
    ASSERT_TRUE(winners_contains(28));
    ASSERT_TRUE(winners_contains(27));
    ASSERT_TRUE(winners_contains(reps[0]));
    ASSERT_TRUE(winners_contains(reps[1]));
    ASSERT_FALSE(winners_contains(reps[2]));
    ASSERT_TRUE(winners_contains(reps[3]));
    ASSERT_TRUE(winners_contains(reps[4]));

    transition_epoch();

    ASSERT_EQ(get_candidates().size(), 32);

    auto contains = [&eb](auto account)
    {
        for(auto del : eb.delegates)
        {
            if(del.account == account)
            {
                return true;
            }
        }
        return false;
    };

    ASSERT_TRUE(contains(31));
    ASSERT_TRUE(contains(30));
    ASSERT_FALSE(contains(29));
    ASSERT_TRUE(contains(28));
    ASSERT_TRUE(contains(27));
    ASSERT_TRUE(contains(reps[0]));
    ASSERT_TRUE(contains(reps[1]));
    ASSERT_FALSE(contains(reps[2]));
    ASSERT_TRUE(contains(reps[3]));
    ASSERT_TRUE(contains(reps[4]));

    EpochVotingManager::ENABLE_ELECTIONS = false;

}

TEST(Elections, weighted_votes)
{

    logos::block_store* store = get_db();
    PersistenceManager<R> persistence_mgr(*store,nullptr);
    PersistenceManager<ECT> epoch_persistence_mgr(*store,nullptr);
    clear_dbs();
    logos::transaction txn(store->environment,nullptr,true);
    logos::account_info account_info(0,0,0,0,1000,0,0,0,0);
    logos::account_info account_info2 = account_info;
    std::shared_ptr<StakingManager> sm = StakingManager::GetInstance();
    uint32_t epoch = 10;

    AccountAddress rep_address = 7;
    RepInfo rep;
    store->rep_put(rep_address,rep,txn);
    store->account_put(rep_address,account_info,txn);
    sm->Stake(rep_address,account_info,100,rep_address,epoch,txn);

    AccountAddress rep2_address = 8;
    RepInfo rep2;
    store->rep_put(rep2_address,rep,txn);
    store->account_put(rep2_address,account_info2,txn);
    sm->Stake(rep2_address,account_info2,200,rep2_address,epoch,txn);

    AccountAddress candidate_address = 12;
    CandidateInfo candidate;
    init_ecies(candidate.ecies_key);
    store->candidate_put(candidate_address,candidate,txn);
    VotingPowerManager::GetInstance()->AddSelfStake(candidate_address,10,epoch,txn);

    AccountAddress candidate2_address = 13;
    CandidateInfo candidate2;
    init_ecies(candidate2.ecies_key);
    store->candidate_put(candidate2_address,candidate,txn);
    VotingPowerManager::GetInstance()->AddSelfStake(candidate2_address,10,epoch,txn);
    
    ElectionVote vote;
    vote.epoch_num = ++epoch;
    vote.origin = rep_address;
    vote.votes.emplace_back(candidate_address,8);
    persistence_mgr.ApplyRequest(vote,account_info,txn);

    vote.origin = rep2_address;
    vote.votes.clear();
    vote.votes.emplace_back(candidate_address,4);
    vote.votes.emplace_back(candidate2_address,4);
    persistence_mgr.ApplyRequest(vote,account_info2,txn);


    store->candidate_get(candidate_address,candidate,txn);
    store->candidate_get(candidate2_address,candidate2,txn);

    ASSERT_EQ(candidate.votes_received_weighted,1600);
    ASSERT_EQ(candidate2.votes_received_weighted,800);
}

TEST(Elections, tiebreakers)
{

    Delegate d1(init_delegate(1,10,20,false));
    Delegate d2(init_delegate(2,10,30,false));
    Delegate d3(init_delegate(3,10,30,false));
    Delegate d4(init_delegate(4,100,2,false));

    ASSERT_TRUE(EpochVotingManager::IsGreater(d2,d1));
    ASSERT_TRUE(EpochVotingManager::IsGreater(d2,d3));
    ASSERT_TRUE(EpochVotingManager::IsGreater(d3,d1));
    ASSERT_TRUE(EpochVotingManager::IsGreater(d4,d3));
}

TEST(Elections, remove_db)
{
    logos::block_store* store = get_db();
    clear_dbs();
    PersistenceManager<ECT> epoch_persistence_mgr(*store,nullptr);
    logos::transaction txn(store->environment,nullptr,true);   


    AccountAddress address = 42;

    ASSERT_FALSE(store->candidate_mark_remove(address, txn));
    ASSERT_FALSE(store->candidate_mark_remove(address, txn));

    ASSERT_FALSE(store->rep_mark_remove(address, txn));
    ASSERT_FALSE(store->rep_mark_remove(address, txn));

    address = 45;

    ASSERT_FALSE(store->candidate_mark_remove(address, txn));
    ASSERT_FALSE(store->rep_mark_remove(address, txn));

    size_t num_remove = 0;
    for(auto it = logos::store_iterator(txn, store->remove_candidates_db);
            it != logos::store_iterator(nullptr); ++it)
    {
        num_remove++;
    }
    ASSERT_EQ(num_remove, 2);
    num_remove = 0;
    for(auto it = logos::store_iterator(txn, store->remove_reps_db);
            it != logos::store_iterator(nullptr); ++it)
    {
        num_remove++;
    }
    ASSERT_EQ(num_remove, 2);

    store->clear(store->remove_candidates_db, txn);
    store->clear(store->remove_reps_db, txn);

    CandidateInfo c_info;
    init_ecies(c_info.ecies_key);
    RepInfo r_info;
    std::vector<AccountAddress> persistent;
    for(size_t i = 0; i < 32; ++i)
    {
        address = i;
        ASSERT_FALSE(store->candidate_put(address, c_info, txn));
        ASSERT_FALSE(store->rep_put(address, r_info, txn));
        if(i % 2 == 0 || i % 3 == 0)
        {
            ASSERT_FALSE(store->candidate_mark_remove(address, txn));
            ASSERT_FALSE(store->rep_mark_remove(address, txn));
        }
        else
        {
            persistent.push_back(address);
        }
    }
    std::sort(persistent.begin(), persistent.end());

    epoch_persistence_mgr.UpdateCandidatesDB(txn);
    epoch_persistence_mgr.UpdateRepresentativesDB(txn);

    std::vector<AccountAddress> remaining;
    for(auto it = logos::store_iterator(txn, store->representative_db);
            it != logos::store_iterator(nullptr); ++it)
    {
        remaining.push_back(it->first.uint256());
    }
    std::sort(remaining.begin(), remaining.end());

    ASSERT_EQ(remaining, persistent);

    num_remove = 0;
    for(auto it = logos::store_iterator(txn, store->remove_candidates_db);
            it != logos::store_iterator(nullptr); ++it)
    {
        num_remove++;
    }
    ASSERT_EQ(num_remove, 0);
    num_remove = 0;
    for(auto it = logos::store_iterator(txn, store->remove_reps_db);
            it != logos::store_iterator(nullptr); ++it)
    {
        num_remove++;
    }
    ASSERT_EQ(num_remove, 0);
}


#endif
