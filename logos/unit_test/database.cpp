
#include <gtest/gtest.h>
#include <logos/blockstore.hpp>
#include <logos/unit_test/msg_validator_setup.hpp>
#include <logos/elections/requests.hpp>
#include <logos/common.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
#include <logos/elections/database.hpp>
#include <logos/elections/database_functions.hpp>
#include <logos/node/delegate_identity_manager.hpp>
#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/persistence/persistence.hpp>

#define Unit_Test_Database

#ifdef Unit_Test_Database


//TODO: json serialization?
//TODO: GetOldEpochBlock test?
//TODO: serialization 

TEST (Database, blockstore)
{
    logos::block_store* store(get_db());
    ASSERT_NE(store,nullptr);
    store->clear(store->representative_db);
    store->clear(store->state_db);
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
        EXPECT_EQ(req.type,req2.type);
        EXPECT_EQ(req.previous,req2.previous);
        EXPECT_EQ(req.next,req2.next);
        EXPECT_EQ(req.fee,req2.fee);
        EXPECT_EQ(req.origin,req2.origin);
        EXPECT_EQ(req.sequence,req2.sequence);
        req.Hash();
        req2.Hash();
        EXPECT_EQ(req.digest,req2.digest);
        ASSERT_EQ(req,req2);

        //ElectionVote no votes
        BlockHash prev = 111;
        AccountAddress address = 1;
        AccountSig sig = 1;
        Amount fee = 7;
        uint32_t sequence = 2;
        ElectionVote ev(address, prev, fee, sequence, sig);
        ev.epoch_num = 42;

        
        auto hash = ev.Hash();
        res = store->request_put(ev,txn);

        ASSERT_FALSE(res);

        ElectionVote ev2;
        ev2.type = RequestType::ElectionVote;
        std::cout << "getting ev2" << std::endl;
        res = store->request_get(hash,ev2,txn);
        std::cout << "got ev2" << std::endl;
        ASSERT_FALSE(res);
        ASSERT_EQ(ev2.type,ev.type);
        ASSERT_EQ(ev2.previous,ev.previous);
        ASSERT_EQ(ev2.origin,ev.origin);
        ASSERT_EQ(ev2.signature,ev.signature);
        ASSERT_EQ(ev2.fee,ev.fee);
        ASSERT_EQ(ev2.sequence,ev.sequence);
        ASSERT_EQ(ev2.votes,ev.votes);
        ASSERT_EQ(ev2.digest,ev.digest);
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

        std::cout << "putting" << std::endl;
        hash = ev.Hash();
        res = store->request_put(ev,txn);
        ASSERT_FALSE(res);

        ElectionVote ev3;
        ev3.type = RequestType::ElectionVote;
        res = store->request_get(ev.Hash(),ev3,txn);
        ASSERT_FALSE(res);
        ASSERT_EQ(ev3.type,ev.type);
        ASSERT_EQ(ev3.previous,ev.previous);
        ASSERT_EQ(ev3.origin,ev.origin);
        ASSERT_EQ(ev3.signature,ev.signature);
        ASSERT_EQ(ev3.fee,ev.fee);
        ASSERT_EQ(ev3.sequence,ev.sequence);
        ASSERT_EQ(ev3.votes,ev.votes);
        ASSERT_EQ(ev3.digest,ev.digest);
        ASSERT_EQ(ev,ev3);
        ASSERT_NE(ev3,ev2);

        ev_json = ElectionVote(res, ev3.SerializeJson());
        ASSERT_FALSE(res);
        ASSERT_EQ(ev_json, ev3);


        AnnounceCandidacy announce(7,12,23,2);
        announce.stake = 4;
        announce.bls_key = 13;
        announce.epoch_num = 11;

        ASSERT_FALSE(store->request_put(announce,txn));
        AnnounceCandidacy announce2;
        ASSERT_FALSE(store->request_get(announce.Hash(),announce2,txn));
        ASSERT_EQ(announce2.type,RequestType::AnnounceCandidacy);
        ASSERT_EQ(announce.stake,announce2.stake);
        ASSERT_EQ(announce,announce2);

        AnnounceCandidacy announce_json(res, announce.SerializeJson());
        ASSERT_FALSE(res);
        ASSERT_EQ(announce_json, announce);

        RenounceCandidacy renounce(2,3,5,7);
        renounce.epoch_num = 26;
        ASSERT_FALSE(store->request_put(renounce,txn));
        RenounceCandidacy renounce2;
        ASSERT_FALSE(store->request_get(renounce.Hash(),renounce2,txn));
        ASSERT_EQ(renounce,renounce2);
        RenounceCandidacy renounce_json(res, renounce.SerializeJson());
        
        ASSERT_FALSE(res);

        ASSERT_EQ(renounce_json, renounce);

        StartRepresenting start(4,5,2,3,32);
        start.epoch_num = 456;
        ASSERT_FALSE(store->request_put(start,txn));
        StartRepresenting start2;
        ASSERT_EQ(GetRequestType<StartRepresenting>(),RequestType::StartRepresenting);
        ASSERT_FALSE(store->request_get(start.Hash(),start2,txn));
        ASSERT_EQ(start.stake,start2.stake);
        ASSERT_EQ(start,start2);

        StartRepresenting start_json(res, start.SerializeJson());
        ASSERT_FALSE(res);

        ASSERT_EQ(start_json, start);


        StopRepresenting stop(4,5,2,3,32);
        stop.epoch_num = 456;
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
        rep_info.active = true;
        rep_info.remove = true;
        rep_info.voted = true;
        rep_info.stake = 37;

        res = store->rep_put(rep_account,rep_info,txn);
        ASSERT_FALSE(res);

        RepInfo rep_info2;
        res = store->rep_get(rep_account,rep_info2,txn);
        ASSERT_FALSE(res);
        ASSERT_EQ(rep_info,rep_info2);


        CandidateInfo candidate_info;
        AccountAddress candidate_account;
        candidate_info.active = true;
        candidate_info.remove = true;
        candidate_info.stake = 42;
        candidate_info.bls_key = 3;

        ASSERT_FALSE(store->candidate_put(candidate_account,candidate_info,txn));

        CandidateInfo candidate_info2;
        ASSERT_FALSE(store->candidate_get(candidate_account,candidate_info2,txn));
        ASSERT_EQ(candidate_info,candidate_info2);




    }
   
}

TEST(Database, heap)
{
    std::vector<int> nums;
    nums.resize(100);
    {
        for(size_t i = 0; i < nums.size(); ++i)
        {
            nums[i] = i;
        }

        FixedSizeHeap<int> heap(8,[](int a, int b){return a > b;});
        for(int i : nums)
        {
            heap.try_push(i);
        }

        std::vector<int> res_exp{99,98,97,96,95,94,93,92};
        ASSERT_EQ(res_exp,heap.getResults());
    }
    {
        for(size_t i = 0; i < nums.size(); ++i)
        {
            if(i%10 == 0)
            {
                nums[i] *= 10;
            }
        }

        FixedSizeHeap<int> heap(8,[](int a, int b){return a > b;});
        for(int i : nums)
        {
            heap.try_push(i); 
        }

        std::vector<int> res_exp{900,800,700,600,500,400,300,200};
        ASSERT_EQ(res_exp,heap.getResults());
    }
   
    {
        FixedSizeHeap<int> heap(8,[](int a, int b){return a > b;});
        heap.try_push(10);
        heap.try_push(12);
        std::vector<int> res_exp{12,10};
        ASSERT_EQ(res_exp,heap.getResults());
    } 

}

TEST(Database, candidates_simple)
{
    
    logos::block_store* store = get_db();
    store->clear(store->candidacy_db);
    
    CandidateInfo c1(true,false,100);
    c1.stake = 34;
    c1.bls_key = 4;
    AccountAddress a1(0);
    CandidateInfo c2(false,false,110);
    c2.stake = 456;
    c2.bls_key = 7;
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

        res = store->candidate_add_vote(a1,100,txn);
        ASSERT_FALSE(res);
        res = store->candidate_add_vote(a1,50,txn);
        ASSERT_FALSE(res);

//        CandidateInfo c3(true,false,250);
//        res = store->candidate_put(a1,c3,txn);
//        ASSERT_FALSE(res);
//        ASSERT_EQ(c3.votesreceived_weighted,c1.votesreceived_weighted+100+50);

        CandidateInfo c3_copy;
        res = store->candidate_get(a1,c3_copy,txn);
        ASSERT_FALSE(res);
        ASSERT_EQ(c3_copy.votes_received_weighted,c1.votes_received_weighted+100+50);
        
        res = store->candidate_add_vote(a2,100,txn);
        ASSERT_TRUE(res);

        AccountAddress a3(2);
        res = store->candidate_add_vote(a3,100,txn);
        ASSERT_TRUE(res);
        
    }

    //TODO: write tests for candidate functions
}

TEST(Database, get_winners)
{

    logos::block_store* store = get_db();
    store->clear(store->candidacy_db);
    store->clear(store->leading_candidates_db);

    EpochVotingManager mgr(*store);

    size_t num_winners = 8;
    auto winners = mgr.GetElectionWinners(num_winners);
    ASSERT_EQ(winners.size(),0);
    
    std::vector<std::pair<AccountAddress,CandidateInfo>> candidates;
    size_t num_candidates = 100;
    for(size_t i = 0; i < num_candidates; ++i)
    {
        logos::transaction txn(store->environment,nullptr,true);
        CandidateInfo c(false,false,(i % 3) * 100 + i);
        c.bls_key = i * 4 + 37;
        AccountAddress a(i);
        std::cout << "i is " << i << std::endl;
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
//    for(auto p : results)
//    {
//        std::cout << "account is : " << p.first.to_string() << std::endl;
//    }
//
//    for(auto c : winners)
//    {
//        std::cout << "account is : " << c.first.to_string() << std::endl;
//    }
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

TEST(Database, representatives_db)
{
    logos::block_store* store = get_db();
    store->clear(store->representative_db);
    logos::transaction txn(store->environment,nullptr,true);

    AccountAddress rep_address;
    RepInfo rep;
    rep.candidacy_action_tip = 12;
    rep.election_vote_tip = 4;
    rep.rep_action_tip = 42;
    rep.voted = false;
    rep.active = false;
    rep.remove = false;

    store->rep_put(rep_address,rep,txn);
    RepInfo rep2;
    store->rep_get(rep_address,rep2,txn);
    ASSERT_EQ(rep,rep2);

    PersistenceManager<ECT> mgr(*store,nullptr);

    mgr.UpdateRepresentativesDB(txn);

    ASSERT_FALSE(store->rep_get(rep_address, rep2, txn));
    ASSERT_TRUE(rep2.active);
    rep.active = true;
    rep.voted = true;
    store->rep_put(rep_address, rep, txn);
    mgr.UpdateRepresentativesDB(txn);
    ASSERT_FALSE(store->rep_get(rep_address, rep2, txn));
    ASSERT_FALSE(rep2.voted);

    rep.remove = true;
    store->rep_put(rep_address, rep, txn);
    mgr.UpdateRepresentativesDB(txn);
    
    ASSERT_TRUE(store->rep_get(rep_address, rep2, txn));

    
}

TEST(Database,candidates_transition)
{
    logos::block_store* store = get_db();
    store->clear(store->candidacy_db);
    store->clear(store->epoch_db);
    store->clear(store->epoch_tip_db);

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
        bool res = store->candidate_add_new(a1,bls1,stake1,txn);
        ASSERT_FALSE(res);
        res = store->candidate_add_new(a2,bls2,stake2,txn);
        ASSERT_FALSE(res);
    }
    iterateCandidatesDB(*store,[](auto& it){
            bool error = false;
            CandidateInfo info(error,it->second);
            ASSERT_FALSE(error);
            ASSERT_FALSE(info.active);
            ASSERT_FALSE(info.remove);
            ASSERT_EQ(info.votes_received_weighted,0);
            },txn);       

    mgr.UpdateCandidatesDB(txn);

    iterateCandidatesDB(*store,[](auto& it){
            bool error = false;
            CandidateInfo info(error,it->second);
            ASSERT_FALSE(error);
            ASSERT_TRUE(info.active);
            ASSERT_FALSE(info.remove);
            ASSERT_EQ(info.votes_received_weighted,0);
            },txn);

    {
        bool res = store->candidate_mark_remove(a1,txn);
        ASSERT_FALSE(res);
        CandidateInfo info;
        res = store->candidate_get(a1,info,txn);
        ASSERT_FALSE(res);
        ASSERT_TRUE(info.remove);
        ASSERT_TRUE(info.active);
        res = store->candidate_add_new(a3,bls3,stake3,txn);
        ASSERT_FALSE(res);
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

    iterateCandidatesDB(*store,[](auto& it){
            bool error = false;
            CandidateInfo info(error,it->second);
            ASSERT_FALSE(error);
            ASSERT_TRUE(info.active);
            ASSERT_FALSE(info.remove);
            },txn);
    {
        ApprovedEB eb;
        eb.delegates[0].account = a2;
        eb.delegates[0].starting_term = true;

        ASSERT_FALSE(store->epoch_put(eb,txn));
        ASSERT_FALSE(store->epoch_tip_put(eb.Hash(),txn));

    }    
    uint32_t no_reelections = EpochVotingManager::START_ELECTIONS_EPOCH - 1;
    uint32_t reelections = EpochVotingManager::START_ELECTIONS_EPOCH;
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
            BlockHash hash;
            ASSERT_FALSE(store->epoch_tip_get(hash,txn));
            eb.previous = hash;
            eb.delegates[0].starting_term = false;
            ASSERT_FALSE(store->epoch_put(eb,txn));
            eb.previous = eb.Hash();
            ASSERT_FALSE(store->epoch_put(eb,txn));
            ASSERT_FALSE(store->epoch_tip_put(eb.Hash(),txn));
        }

        mgr.MarkDelegateElectsAsRemove(txn);
        mgr.UpdateCandidatesDB(txn);
        {
            CandidateInfo info;
            bool res = store->candidate_get(a2,info,txn);
            ASSERT_TRUE(res);
            eb.previous = eb.Hash();
            ASSERT_FALSE(store->epoch_put(eb,txn));
            ASSERT_FALSE(store->epoch_tip_put(eb.Hash(),txn));
            RepInfo rep;
            RenounceCandidacy req;
            req.origin = a2;
            rep.candidacy_action_tip = req.Hash();
            store->request_put(req,txn);
            store->rep_put(a2,rep,txn);
        }
    }


    mgr.TransitionNextEpoch(txn, EpochVotingManager::START_ELECTIONS_EPOCH+1);

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

    mgr.TransitionNextEpoch(txn, EpochVotingManager::START_ELECTIONS_EPOCH+1);
    {
        CandidateInfo info;
        bool res = store->candidate_get(a2,info,txn);
        ASSERT_FALSE(res);
    }

    store->candidate_add_vote(a2,100,txn);

    {
        CandidateInfo info;
        store->candidate_get(a2,info,txn);
        ASSERT_EQ(info.votes_received_weighted,100);
    }
    
    mgr.MarkDelegateElectsAsRemove(txn);
    mgr.UpdateCandidatesDB(txn);

    {
        CandidateInfo info;
        store->candidate_get(a2,info,txn);
        ASSERT_EQ(info.votes_received_weighted,0);
    }

}

TEST(Database,get_next_epoch_delegates)
{
    logos::block_store* store = get_db();
    store->clear(store->candidacy_db);
    store->clear(store->epoch_db);
    store->clear(store->epoch_tip_db);
    DelegateIdentityManager::_epoch_transition_enabled = true;

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
        ASSERT_FALSE(store->epoch_tip_put(eb.Hash(),txn));
        ASSERT_FALSE(store->epoch_put(eb,txn));
    }

    EpochVotingManager::START_ELECTIONS_EPOCH = 4;

    auto transition_epoch = [&](int retire_idx = -1)
    {
        std::cout << "transitioning to epoch " << (epoch_num + 1) << std::endl;
        ++epoch_num;
        eb.previous = eb.Hash();
        eb.epoch_number = epoch_num-1;
        logos::transaction txn(store->environment,nullptr,true);
        voting_mgr.GetNextEpochDelegates(eb.delegates,epoch_num);
        ASSERT_FALSE(store->epoch_tip_put(eb.Hash(),txn));
        ASSERT_FALSE(store->epoch_put(eb,txn));
        persistence_mgr.TransitionCandidatesDBNextEpoch(txn, epoch_num);

    };

    auto compare_delegates = [&eb,&delegates]()
    {
        for(size_t i = 0; i < 32; ++i)
        {
            std::cout << "i is " << i << std::endl;
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
            store->candidate_add_vote(delegates[i].account,new_vote,txn);
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
            store->candidate_add_vote(delegates[i].account,new_vote,txn);
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
            store->candidate_add_vote(delegates[i].account,new_vote,txn);
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
            store->candidate_add_vote(delegates[i].account,new_vote,txn);
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
        std::cout << "******LOOP NUMBER " << e << std::endl;
    candidates = get_candidates();
    ASSERT_EQ(candidates.size(),8);
    ASSERT_EQ(voting_mgr.GetRetiringDelegates(epoch_num+1).size(),8);
        {
            logos::transaction txn(store->environment,nullptr,true);
            for(size_t i = 24; i < 32; ++i)
            {
                auto new_vote = delegates[i].vote + 500;
                ASSERT_FALSE(store->candidate_add_vote(delegates[i].account,new_vote,txn));
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

}

TEST(Database, redistribute_votes)
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

TEST(Database,validate)
{
    logos::block_store* store = get_db();
    PersistenceManager<R> persistence_mgr(*store,nullptr);
    PersistenceManager<ECT> epoch_persistence_mgr(*store,nullptr);
    store->clear(store->candidacy_db);
    store->clear(store->representative_db);
    store->clear(store->epoch_db);
    store->clear(store->epoch_tip_db);
    logos::transaction txn(store->environment,nullptr,true);

    logos::process_return result;
    result.code = logos::process_result::progress; 
    DelegateIdentityManager::_epoch_transition_enabled = true;
    AccountAddress sender_account = 100;
    AccountAddress sender_account2 = 101;
    EpochVotingManager::START_ELECTIONS_EPOCH = 2;

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
    //no epoch block created yet, everything should fail
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));

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
    ASSERT_FALSE(store->epoch_tip_put(eb.Hash(),txn));
    ASSERT_FALSE(store->epoch_put(eb,txn));

    //epoch block created, but only StartRepresenting should pass
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
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
        std::cout << "transitioning to epoch " << epoch_num << std::endl;
        eb.previous = eb.Hash();
        eb.epoch_number = epoch_num-1;
        vote.epoch_num = epoch_num;
        announce.epoch_num = epoch_num;
        renounce.epoch_num = epoch_num;
        start_rep.epoch_num = epoch_num;
        stop_rep.epoch_num = epoch_num;
        bool reelection = epoch_num > 3;
        for(Delegate& del : eb.delegates)
        {
            del.starting_term = false;
        }
        for(size_t i = 0; i < new_delegates.size(); ++i)
        {
            std::cout << "adding new delegate" << std::endl;
            eb.delegates[i].account = new_delegates[i];
            eb.delegates[i].starting_term = true;
        }
        ASSERT_FALSE(store->epoch_tip_put(eb.Hash(),txn));
        ASSERT_FALSE(store->epoch_put(eb,txn));
        epoch_persistence_mgr.TransitionNextEpoch(txn,epoch_num > 3 ? epoch_num : 0);
    };


    auto get_candidates = [&store,&txn](auto filter) -> std::vector<CandidateInfo>
    {
        std::vector<CandidateInfo> results;
        for(auto it = logos::store_iterator(txn, store->candidacy_db);
                it != logos::store_iterator(nullptr); ++it)
        {
            bool error = false;
            CandidateInfo info(error,it->second);
            assert(!error);
            if(filter(info))
            {
                results.push_back(info);
            }
        }
        return results;
    };

    std::cout << "first transition" << std::endl;
    transition_epoch();

    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));


    persistence_mgr.ApplyRequest(vote,txn);
    std::cout << "VOTED ************" << std::endl;
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    persistence_mgr.ApplyRequest(announce,txn);
    std::cout << "ANNOUNCED *************" << std::endl;
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    std::cout << "1 ************" << std::endl;
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));
    std::cout << "2 ***********" << std::endl;

    auto active = [](auto info) -> bool
    {
        return info.active;
    };

    auto all = [](auto info) -> bool
    {
        return true;
    };

    auto remove = [](auto info) -> bool
    {
        return info.remove;
    };

    auto candidates = get_candidates(all);
    ASSERT_EQ(candidates.size(),1);
    candidates = get_candidates(active);
    ASSERT_EQ(candidates.size(),0);
    std::cout << "second transition *********" << std::endl;
    transition_epoch();

    ASSERT_EQ(get_candidates(all).size(),1);
    ASSERT_EQ(get_candidates(active).size(),1);

    std::cout << "TRANSITIONED *************" << std::endl;
    CandidateInfo info;
    ASSERT_FALSE(store->candidate_get(announce.origin,info,txn));
    ASSERT_TRUE(info.active);
    ASSERT_FALSE(info.remove);
    ASSERT_EQ(info.votes_received_weighted,0);

    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));

    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    std::cout << "APPLYING RENOUNCE" << std::endl;

    persistence_mgr.ApplyRequest(renounce,txn);

    ASSERT_EQ(get_candidates(all).size(),1);
    ASSERT_EQ(get_candidates(active).size(),1);
    ASSERT_EQ(get_candidates(remove).size(),1);
    std::cout << "APPLIED RENOUNCE" << std::endl;

    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));
    std::cout << "1 ********" << std::endl;
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));

    std::cout << "CHECKED VALIDITY" << std::endl;

    vote.votes.emplace_back(sender_account,8);
    vote.Hash();
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    persistence_mgr.ApplyRequest(vote,txn);

    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    std::cout << "ABOUT TO TRANSITION" << std::endl;

    transition_epoch();

    ASSERT_EQ(get_candidates(all).size(),0);

    
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    vote.votes.clear();

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
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));

    persistence_mgr.ApplyRequest(start_rep,txn);

    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));

    transition_epoch();

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
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_TRUE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));


    std::vector<AccountAddress> new_delegates;
    new_delegates.push_back(announce.origin);
    std::cout << "transitioning with new delegates" << std::endl;
    transition_epoch(new_delegates);
    std::cout << "transitioned with new delegates" << std::endl;

   
    ASSERT_FALSE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));

    vote.votes.emplace_back(announce.origin,8);
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    vote.votes.clear();

    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(start_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
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
    ASSERT_FALSE(persistence_mgr.ValidateRequest(stop_rep,epoch_num,txn,result));
    ASSERT_FALSE(persistence_mgr.ValidateRequest(renounce,epoch_num,txn,result));

    transition_epoch();

    transition_epoch();

    vote.votes.emplace_back(announce.origin,8);
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));

    ASSERT_TRUE(persistence_mgr.ValidateRequest(announce,epoch_num,txn,result));
    persistence_mgr.ApplyRequest(announce,txn);
        
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    transition_epoch();
    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));

    transition_epoch({announce.origin});
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    transition_epoch();

    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    transition_epoch();
    
    ASSERT_FALSE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));
    transition_epoch();

    ASSERT_TRUE(persistence_mgr.ValidateRequest(vote,epoch_num,txn,result));


}

TEST(Database, transaction)
{

    logos::block_store* store = get_db();
    {
    logos::transaction txn(store->environment,nullptr,true);
    std::cout << "opened first transaction" << std::endl;

    std::thread new_thread([&store]()
            {
                std::cout << "opening second transaction" << std::endl;
                logos::transaction txn(store->environment,nullptr,true);
                std::cout << "opened second transaction" << std::endl;

            }
            );

     new_thread.detach();

    usleep(10000000);

    std::cout << "woke up" << std::endl;
    }
    std::cout << "closed first transaction" << std::endl;
    usleep(10000000);


}


TEST(Database, full)
{

    logos::block_store* store = get_db();
    store->clear(store->candidacy_db);
    store->clear(store->epoch_db);
    store->clear(store->epoch_tip_db);
    store->clear(store->representative_db);
    DelegateIdentityManager::_epoch_transition_enabled = true;

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
        ASSERT_FALSE(store->epoch_tip_put(eb.Hash(),txn));
        ASSERT_FALSE(store->epoch_put(eb,txn));
    }

    EpochVotingManager::START_ELECTIONS_EPOCH = 4;






        

    auto transition_epoch = [&]()
    {
        ++epoch_num;
        std::cout << "transitioning to epoch " << epoch_num << std::endl;
        eb.previous = eb.Hash();
        eb.epoch_number = epoch_num-1;
        voting_mgr.GetNextEpochDelegates(eb.delegates,epoch_num);
        epoch_persistence_mgr.ApplyUpdates(eb);
    };

    auto get_candidates = [&store](auto filter) -> std::vector<CandidateInfo>
    {

        logos::transaction txn(store->environment,nullptr,true);
        std::vector<CandidateInfo> results;
        for(auto it = logos::store_iterator(txn, store->candidacy_db);
                it != logos::store_iterator(nullptr); ++it)
        {
            bool error = false;
            CandidateInfo info(error,it->second);
            assert(!error);
            if(filter(info))
            {
                results.push_back(info);
            }
        }
        return results;
    };

    auto active = [](auto info) -> bool
    {
        return info.active;
    };

    auto all = [](auto info) -> bool
    {
        return true;
    };

    auto remove = [](auto info) -> bool
    {
        return info.remove;
    };

    std::vector<AccountAddress> reps;

    for(size_t i = 0; i < 16; ++i)
    {
        logos::transaction txn(store->environment, nullptr, true);
        StartRepresenting start_rep;
        start_rep.origin = 100+i;
        start_rep.stake = 10 + 10 * (i%2);
        start_rep.epoch_num = epoch_num;
        logos::process_return result;
        ASSERT_TRUE(req_persistence_mgr.ValidateRequest(start_rep, epoch_num, txn, result));
        req_persistence_mgr.ApplyRequest(start_rep,txn);
        reps.push_back(start_rep.origin);
    }

    ASSERT_EQ(get_candidates(all).size(),0);
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

            logos::process_return result;
            ASSERT_TRUE(req_persistence_mgr.ValidateRequest(announce, epoch_num, txn, result));
            req_persistence_mgr.ApplyRequest(announce, txn);
        }
    }

    transition_epoch();

    ASSERT_EQ(get_candidates(all).size(),40);

    {
    logos::transaction txn(store->environment, nullptr, true);

    logos::process_return result;

    ElectionVote ev;
    ev.origin = reps[0];
    ev.votes.emplace_back(eb.delegates[0].account,8);
    ev.epoch_num = epoch_num;
    if(!req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result))
    {
        std::cout << ProcessResultToString(result.code) << std::endl;
        assert(false);
    }
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[1];
    ev.votes.emplace_back(eb.delegates[1].account,8);
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[2];
    ev.votes.emplace_back(eb.delegates[2].account,8);
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[3];
    ev.votes.emplace_back(eb.delegates[3].account,8);
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[4];
    ev.votes.emplace_back(reps[0],8);
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[5];
    ev.votes.emplace_back(reps[1],8);
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[6];
    ev.votes.emplace_back(reps[2],8);
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[7];
    ev.votes.emplace_back(reps[3],8);
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[8];
    ev.votes.emplace_back(reps[4],8);
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[9];
    ev.votes.emplace_back(reps[4],8);
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[10];
    ev.votes.emplace_back(eb.delegates[4].account,8);
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[11];
    ev.votes.emplace_back(eb.delegates[4].account,8);
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[12];
    ev.votes.emplace_back(reps[0],8);
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[13];
    ev.votes.emplace_back(reps[0],8);
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[14];
    ev.votes.emplace_back(eb.delegates[0].account,8);
    ASSERT_TRUE(req_persistence_mgr.ValidateRequest(ev, epoch_num, txn, result));
    req_persistence_mgr.ApplyRequest(ev, txn);

    ev.votes.clear();
    ev.origin = reps[15];
    ev.votes.emplace_back(eb.delegates[0].account,8);
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

    ASSERT_EQ(get_candidates(all).size(), 32);

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
    







//    std::vector<std::pair<AccountAddress,DelegatePubKey>> accounts;
//    for(size_t i = 32; i < 64; ++i)
//    {
//        accounts.emplace_back(i,i);
//    }
//
//    for(auto account : accounts)
//    {
//        StartRepresenting sr;
//        sr.origin = account.first;
//        sr.stake = MIN_REP_STAKE;
//        sr.epoch_num = epoch_num;
//        sr.Hash();
//        ASSERT_TRUE(persistence_mgr.ValidateRequest(sr,epoch_num,txn,result));
//        persistence_mgr.ApplyRequest(sr,txn);
//    }
//
//
//    for(auto account : accounts)
//    {
//        AnnounceCandidacy ac;
//        ac.origin = account.first;
//        ac.stake = MIN_DELEGATE_STAKE;
//        ac.bls_key = account.second;
//        ac.epoch_num = epoch_num;
//        ASSERT_TRUE(persistence_mgr.ValidateRequest(ac,epoch_num,txn,result));
//        persistence_mgr.ApplyRequest(ac,txn);
//    }
//
//    ASSERT_EQ(get_candidates(all).size(),accounts.size());
//    ASSERT_EQ(get_candidates(active).size(),0);
//
//    transition_epoch();
//    ASSERT_EQ(get_candidates(active).size(),accounts.size());
//
//    for(size_t i = 0; i < accounts.size(); ++i)
//    {
//        ElectionVote v;
//        v.origin = accounts[i].first;
//        v.votes.emplace_back(accounts[i%8].first,8);
//        v.epoch_num = epoch_num;
//        ASSERT_TRUE(persistence_mgr.ValidateRequest(v,epoch_num,txn,result));
//        persistence_mgr.ApplyRequest(v,txn);
//    }
//
//    transition_epoch();
}

TEST(Database, weighted_votes)
{

    logos::block_store* store = get_db();
    PersistenceManager<R> persistence_mgr(*store,nullptr);
    PersistenceManager<ECT> epoch_persistence_mgr(*store,nullptr);
    store->clear(store->candidacy_db);
    store->clear(store->representative_db);
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
    candidate.active = true;
    store->candidate_put(candidate_address,candidate,txn);

    AccountAddress candidate2_address = 13;
    CandidateInfo candidate2;
    candidate2.active = true;
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

TEST(Database, tiebreakers)
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





#endif
