
#include <gtest/gtest.h>
#include <logos/blockstore.hpp>
#include <logos/unit_test/msg_validator_setup.hpp>
#include <logos/elections/requests.hpp>
#include <logos/common.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
#include <logos/elections/candidate.hpp>
#include <logos/elections/representative.hpp>
#include <logos/node/delegate_identity_manager.hpp>
#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/persistence/persistence.hpp>

#define Unit_Test_Elections

#ifdef Unit_Test_Elections

void clear_dbs()
{
    logos::block_store* store = get_db();
    store->clear(store->candidacy_db);
    store->clear(store->representative_db);
    store->clear(store->epoch_db);
    store->clear(store->epoch_tip_db);
    store->clear(store->remove_candidates_db);
    store->clear(store->remove_reps_db);
    store->clear(store->state_db);
    store->clear(store->leading_candidates_db);
    store->leading_candidates_size = 0;
}

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
        announce.origin = 7;
        announce.previous = 12;
        announce.sequence = 23;
        announce.fee = 2;
        announce.stake = 4;
        announce.bls_key = 13;
        announce.epoch_num = 11;
        announce.Hash();

        ASSERT_FALSE(store->request_put(announce,txn));
        AnnounceCandidacy announce2;
        ASSERT_FALSE(store->request_get(announce.Hash(),announce2,txn));
        ASSERT_EQ(announce2.type,RequestType::AnnounceCandidacy);
        ASSERT_EQ(announce.stake,announce2.stake);
        ASSERT_EQ(announce,announce2);

        AnnounceCandidacy announce_json(res, announce.SerializeJson());
        ASSERT_FALSE(res);
        ASSERT_EQ(announce_json, announce);

        RenounceCandidacy renounce;
        renounce.origin = 2;
        renounce.previous = 3;
        renounce.sequence = 5;
        renounce.signature = 7;
        renounce.epoch_num = 26;
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
        start.epoch_num = 456;
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
        rep_info.stake = 37;

        res = store->rep_put(rep_account,rep_info,txn);
        ASSERT_FALSE(res);

        RepInfo rep_info2;
        res = store->rep_get(rep_account,rep_info2,txn);
        ASSERT_FALSE(res);
        ASSERT_EQ(rep_info,rep_info2);


        CandidateInfo candidate_info;
        AccountAddress candidate_account;
        candidate_info.stake = 42;
        candidate_info.bls_key = 3;
        candidate_info.epoch_modified = 67;

        ASSERT_FALSE(store->candidate_put(candidate_account,candidate_info,txn));

        CandidateInfo candidate_info2;
        ASSERT_FALSE(store->candidate_get(candidate_account,candidate_info2,txn));
        ASSERT_EQ(candidate_info,candidate_info2);
    }
   
}

TEST(Elections, candidates_simple)
{
    
    logos::block_store* store = get_db();
    clear_dbs();
    
    CandidateInfo c1(100);
    c1.stake = 34;
    c1.bls_key = 4;
    c1.epoch_modified = 12;
    AccountAddress a1(0);
    CandidateInfo c2(110);
    c2.stake = 456;
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
        res = store->candidate_get(a1,c1_copy,txn);
        ASSERT_FALSE(res);
        ASSERT_EQ(c1,c1_copy);

        CandidateInfo c2_copy;
        res = store->candidate_get(a2,c2_copy,txn);
        ASSERT_FALSE(res);
        ASSERT_EQ(c2,c2_copy);

        res = store->candidate_add_vote(a1,100,c1.epoch_modified,txn);
        ASSERT_FALSE(res);
        res = store->candidate_add_vote(a1,50,c1.epoch_modified,txn);
        ASSERT_FALSE(res);

        CandidateInfo c3_copy;
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
    Amount stake1(0);
    Amount stake2(1);
    Amount stake3(2);

    PersistenceManager<ECT> mgr(*store,nullptr);

    logos::transaction txn(store->environment,nullptr,true);
    {
        CandidateInfo candidate;
        candidate.stake = stake1;
        candidate.bls_key = bls1;
        ASSERT_FALSE(store->candidate_put(a1, candidate, txn));
        candidate.stake = stake2;
        candidate.bls_key = bls2;
        ASSERT_FALSE(store->candidate_put(a2, candidate, txn));
    }

    iterateCandidatesDB(*store,[](auto& it){
            bool error = false;
            CandidateInfo info(error,it->second);
            ASSERT_FALSE(error);
            ASSERT_EQ(info.votes_received_weighted,0);
            },txn);       

    {
        bool res = store->candidate_mark_remove(a1,txn);
        ASSERT_FALSE(res);
        CandidateInfo info;
        ASSERT_FALSE(store->candidate_get(a1,info,txn));
        CandidateInfo candidate;
        candidate.stake = stake3;
        candidate.bls_key = bls3;
        ASSERT_FALSE(store->candidate_put(a3, candidate, txn));
    }

    mgr.UpdateCandidatesDB(txn);

    {
        CandidateInfo info;
        bool res = store->candidate_get(a1,info,txn);
        ASSERT_TRUE(res);
        res = store->candidate_get(a2,info,txn);
        ASSERT_FALSE(res);
        res = store->candidate_get(a3,info,txn);
        ASSERT_FALSE(res);
    }


    {
        ApprovedEB eb;
        eb.delegates[0].account = a2;
        eb.delegates[0].starting_term = true;

        ASSERT_FALSE(store->epoch_put(eb,txn));
        ASSERT_FALSE(store->epoch_tip_put(eb.CreateTip(),txn));

    }    

    mgr.MarkDelegateElectsAsRemove(txn);
    mgr.UpdateCandidatesDB(txn);


    {
        CandidateInfo info;
        bool res = store->candidate_get(a2,info,txn);
        ASSERT_TRUE(res);
        res = store->candidate_get(a3,info,txn);
        ASSERT_FALSE(res);
    }

    {

        ApprovedEB eb;
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

    mgr.AddReelectionCandidates(txn);

    {
        CandidateInfo info;
        bool res = store->candidate_get(a2,info,txn);
        ASSERT_TRUE(res);
    }

    AnnounceCandidacy req;
    req.origin = a2;
    RepInfo rep;
    rep.candidacy_action_tip = req.Hash();
    store->request_put(req,txn);
    store->rep_put(a2,rep,txn);

    mgr.AddReelectionCandidates(txn);
    {
        CandidateInfo info;
        bool res = store->candidate_get(a2,info,txn);
        ASSERT_FALSE(res);
    }
}

TEST(Elections,get_next_epoch_delegates)
{
    logos::block_store* store = get_db();
    clear_dbs();
    DelegateIdentityManager::_epoch_transition_enabled = true;

    EpochVotingManager::ENABLE_ELECTIONS = true;

    uint32_t epoch_num = 1;
    ApprovedEB eb;
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

        Delegate d(i,i,base_vote+i,i);
        d.starting_term = true;
        eb.delegates[i] = d;
        delegates.push_back(d);

        RepInfo rep;
        rep.stake = i; 

        AnnounceCandidacy announce;
        announce.origin = i;
        announce.stake = i;
        announce.bls_key = i;
        rep.candidacy_action_tip = announce.Hash();
        store->request_put(announce,txn);
        
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

    auto compare_delegates = [&eb,&delegates]()
    {
        for(size_t i = 0; i < 32; ++i)
        {
            ASSERT_EQ(eb.delegates[i].account,delegates[i].account);

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


    //Test extension of delegate term

    ASSERT_FALSE(eb.is_extension);
    std::unordered_set<Delegate> retiring = voting_mgr.GetRetiringDelegates(epoch_num+1);
    ApprovedEB retiring_eb;
    store->epoch_get_n(3, retiring_eb,nullptr,[](ApprovedEB& block) { return !block.is_extension;});
    transition_epoch();
    ASSERT_TRUE(eb.is_extension);

    ApprovedEB eb2;
    store->epoch_get_n(0, eb2);
    ASSERT_TRUE(eb2.is_extension);
    for(size_t i = 0; i < NUM_DELEGATES; ++i)
    {
        delegates[i].starting_term = false;
    }

    ApprovedEB retiring_eb2;
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
        Delegate d(i,i,i,i);
        delegates[i] = d;
        sum += i;
    }
    auto cap = sum / 8;
    std::sort(std::begin(delegates),std::end(delegates),
            [](auto d1, auto d2)
            {
            return d1.vote > d2.vote;
            });
    mgr.RedistributeVotes(delegates);

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

    mgr.RedistributeVotes(delegates);

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
    mgr.RedistributeVotes(delegates);
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
    ApprovedEB eb;
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
    DelegateIdentityManager::_epoch_transition_enabled = true;
    AccountAddress sender_account = 100;
    AccountAddress sender_account2 = 101;
    EpochVotingManager::START_ELECTIONS_EPOCH = 2;
    EpochVotingManager::ENABLE_ELECTIONS = true;

    uint32_t epoch_num = 1;
    ElectionVote vote;
    vote.origin = sender_account;
    vote.epoch_num = epoch_num;
    AnnounceCandidacy announce;
    announce.origin = sender_account;
    announce.stake = 1;
    announce.epoch_num = epoch_num;
    RenounceCandidacy renounce;
    renounce.origin = sender_account;
    renounce.epoch_num = epoch_num;
    StartRepresenting start_rep;
    start_rep.origin = sender_account;
    start_rep.stake = 1;
    start_rep.epoch_num = epoch_num;
    StopRepresenting stop_rep;
    stop_rep.origin = sender_account;
    stop_rep.epoch_num = epoch_num;
    announce.Hash();
    renounce.Hash();
    start_rep.Hash();
    stop_rep.Hash();
    vote.Hash();

    ApprovedEB eb;
    eb.epoch_number = epoch_num-1;
    eb.previous = 0;

    for(size_t i = 0; i < 32; ++i)
    {
        Delegate d(i,i,i,i);
        d.starting_term = false; // this is not really neccessary in genesis
        eb.delegates[i] = d;

        RepInfo rep;
        rep.stake = i; 
        store->rep_put(i,rep,txn);
    }
    std::reverse(std::begin(eb.delegates),std::end(eb.delegates));
    ASSERT_FALSE(store->epoch_tip_put(eb.CreateTip(),txn));
    ASSERT_FALSE(store->epoch_put(eb,txn));

    //epoch block created, but only StartRepresenting and AnnounceCandidacy should pass
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));

    persistence_mgr.ApplyRequest(start_rep,txn);

    //all should fail
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    
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
            assert(!error);
            results.push_back(info);
        }
        return results;
    };

    transition_epoch();

    //active rep
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));


    //cast a vote
    persistence_mgr.ApplyRequest(vote,txn);
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    persistence_mgr.ApplyRequest(announce,txn);
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));

    //added as candidate
    auto candidates = get_candidates();
    ASSERT_EQ(candidates.size(),1);
    transition_epoch();

    ASSERT_EQ(get_candidates().size(),1);

    //active candidate
    CandidateInfo info;
    ASSERT_FALSE(store->candidate_get(announce.origin,info,txn));
    ASSERT_EQ(info.votes_received_weighted,0);

    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));

    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));

    persistence_mgr.ApplyRequest(renounce,txn);

    ASSERT_EQ(get_candidates().size(),1);

    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));


    //renounced but still can receive votes
    vote.votes.emplace_back(sender_account,8);
    vote.Hash();
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    persistence_mgr.ApplyRequest(vote,txn);

    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));

    transition_epoch();

    //no longer candidate
    ASSERT_EQ(get_candidates().size(),0);
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    vote.votes.clear();
    vote.Hash();

    //only a rep again
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));

    persistence_mgr.ApplyRequest(stop_rep,txn);
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));



    transition_epoch();
    //no longer rep
    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));

    //announce will also auto add account as rep
    persistence_mgr.ApplyRequest(announce,txn);

    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));

    transition_epoch();

    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    vote.votes.emplace_back(announce.origin,8);
    vote.Hash();
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));

    //stop_rep will also auto renounce candidacy
    persistence_mgr.ApplyRequest(stop_rep,txn);

    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));

    transition_epoch();

    ASSERT_TRUE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));


    persistence_mgr.ApplyRequest(start_rep,txn);

    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));

    transition_epoch();

    vote.votes.clear();
    vote.Hash();
    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));

    persistence_mgr.ApplyRequest(announce,txn);

    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));


    transition_epoch();

    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));


    //add account to delegate set
    std::vector<AccountAddress> new_delegates;
    new_delegates.push_back(announce.origin);
    transition_epoch(new_delegates);
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));

    vote.votes.emplace_back(announce.origin,8);
    vote.Hash();
    //account no longer candidate because account is delegate-elect
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    vote.votes.clear();
    vote.Hash();

    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));

    persistence_mgr.ApplyRequest(renounce,txn);

    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));

    transition_epoch();

    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));

    transition_epoch();

    transition_epoch();

    //verify account is not added for reelection
    vote.votes.emplace_back(announce.origin,8);
    vote.Hash();
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));

    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    persistence_mgr.ApplyRequest(announce,txn);
        
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    transition_epoch();
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));

    //add account to delegate set again
    transition_epoch({announce.origin});
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    transition_epoch();

    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    transition_epoch();
    
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    transition_epoch();

    //verify account was added for reelection
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));

    //test stop_rep for delegates, account not added for reelection
    persistence_mgr.ApplyRequest(stop_rep,txn);

    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));

    transition_epoch({announce.origin});
    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));

    transition_epoch();
    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));

    transition_epoch();
    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));

    transition_epoch();
    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));

    EpochVotingManager::ENABLE_ELECTIONS = false;
}

TEST(Elections, apply)
{

    logos::block_store* store = get_db();
    clear_dbs();
    DelegateIdentityManager::_epoch_transition_enabled = true;

    EpochVotingManager::ENABLE_ELECTIONS = true;

    uint32_t epoch_num = 1;
    ApprovedEB eb;
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

        Delegate d(i,i,base_vote+i,i);
        d.starting_term = true;
        eb.delegates[i] = d;
        delegates.push_back(d);

        RepInfo rep;
        rep.stake = i; 

        AnnounceCandidacy announce;
        announce.origin = i;
        announce.stake = i;
        announce.bls_key = i;
        rep.candidacy_action_tip = announce.Hash();
        store->request_put(announce,txn);
        
        StartRepresenting start_rep;
        start_rep.origin = i;
        start_rep.stake = MIN_DELEGATE_STAKE;
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
        start_rep.stake = 10 + 10 * (i%2);
        start_rep.epoch_num = epoch_num;
        start_rep.Hash();
        logos::process_return result;
        ASSERT_TRUE(req_persistence_mgr.ValidateRequest(start_rep, epoch_num, txn, result));
        req_persistence_mgr.ApplyRequest(start_rep,txn);
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
            announce.origin = account;
            announce.epoch_num = epoch_num;
            announce.stake = 0;
            announce.Hash();

            logos::process_return result;
            ASSERT_TRUE(req_persistence_mgr.ValidateRequest(announce, epoch_num, txn, result));
            req_persistence_mgr.ApplyRequest(announce, txn);
        }
    }

    transition_epoch();

    ASSERT_EQ(get_candidates().size(),40);

    {
    logos::transaction txn(store->environment, nullptr, true);

    logos::process_return result;

    ElectionVote ev;
    ev.origin = reps[0];
    ev.votes.emplace_back(eb.delegates[0].account,8);
    ev.epoch_num = epoch_num;
    ev.Hash();
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[1];
    ev.votes.emplace_back(eb.delegates[1].account,8);
    ev.Hash();
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[2];
    ev.votes.emplace_back(eb.delegates[2].account,8);
    ev.Hash();
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[3];
    ev.votes.emplace_back(eb.delegates[3].account,8);
    ev.Hash();
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[4];
    ev.votes.emplace_back(reps[0],8);
    ev.Hash();
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[5];
    ev.votes.emplace_back(reps[1],8);
    ev.Hash();
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[6];
    ev.votes.emplace_back(reps[2],8);
    ev.Hash();
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[7];
    ev.votes.emplace_back(reps[3],8);
    ev.Hash();
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[8];
    ev.votes.emplace_back(reps[4],8);
    ev.Hash();
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[9];
    ev.votes.emplace_back(reps[4],8);
    ev.Hash();
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[10];
    ev.votes.emplace_back(eb.delegates[4].account,8);
    ev.Hash();
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[11];
    ev.votes.emplace_back(eb.delegates[4].account,8);
    ev.Hash();
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[12];
    ev.votes.emplace_back(reps[0],8);
    ev.Hash();
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[13];
    ev.votes.emplace_back(reps[0],8);
    ev.Hash();
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[14];
    ev.votes.emplace_back(eb.delegates[0].account,8);
    ev.Hash();
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[15];
    ev.votes.emplace_back(eb.delegates[0].account,8);
    ev.Hash();
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);
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
            assert(!error);
            election_results[it->first.uint256()] = info.votes_received_weighted;
        }
    }


    ASSERT_EQ(election_results[29],80);
    ASSERT_EQ(election_results[31],320);
    ASSERT_EQ(election_results[30],160);
    ASSERT_EQ(election_results[28],160);
    ASSERT_EQ(election_results[27],240);
    ASSERT_EQ(election_results[reps[0]],320);
    ASSERT_EQ(election_results[reps[1]],160);
    ASSERT_EQ(election_results[reps[2]],80);
    ASSERT_EQ(election_results[reps[3]],160);
    ASSERT_EQ(election_results[reps[4]],240);


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

    AccountAddress rep_address = 7;
    RepInfo rep;
    rep.stake = 100;
    store->rep_put(rep_address,rep,txn);

    AccountAddress rep2_address = 8;
    RepInfo rep2;
    rep.stake = 200;
    store->rep_put(rep2_address,rep,txn);

    AccountAddress candidate_address = 12;
    CandidateInfo candidate;
    store->candidate_put(candidate_address,candidate,txn);

    AccountAddress candidate2_address = 13;
    CandidateInfo candidate2;
    store->candidate_put(candidate2_address,candidate,txn);
    
    ElectionVote vote;
    vote.origin = rep_address;
    vote.votes.emplace_back(candidate_address,8);
    persistence_mgr.ApplyRequest(vote,txn);

    vote.origin = rep2_address;
    vote.votes.clear();
    vote.votes.emplace_back(candidate_address,4);
    vote.votes.emplace_back(candidate2_address,4);
    persistence_mgr.ApplyRequest(vote,txn);


    store->candidate_get(candidate_address,candidate,txn);
    store->candidate_get(candidate2_address,candidate2,txn);

    ASSERT_EQ(candidate.votes_received_weighted,1600);
    ASSERT_EQ(candidate2.votes_received_weighted,800);
}

TEST(Elections, tiebreakers)
{

    Delegate d1(1,0,10,20);
    Delegate d2(2,0,10,30);
    Delegate d3(3,0,10,30);
    Delegate d4(4,0,100,2);

    ASSERT_TRUE(EpochVotingManager::IsGreater(d2,d1));
    ASSERT_TRUE(EpochVotingManager::IsGreater(d3,d2));
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
