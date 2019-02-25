
#include <gtest/gtest.h>
#include <logos/blockstore.hpp>
#include <logos/unit_test/msg_validator_setup.hpp>
#include <logos/epoch/election_requests.hpp>
#include <logos/common.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
#include <logos/elections/database.hpp>
#include <logos/elections/database_functions.hpp>
#include <logos/node/delegate_identity_manager.hpp>

#define Unit_Test_Database

#ifdef Unit_Test_Database


TEST (Database, blockstore)
{
    logos::block_store* store(get_db());
    ASSERT_NE(store,nullptr);
    store->clear(store->representative_db);
    {
        logos::transaction txn(store->environment,nullptr,true);

        //Generic request
        Request req;
        bool res = store->request_put(req,req.Hash(),txn);
        ASSERT_FALSE(res);

        Request req2;
        res = store->request_get(req.Hash(),req2,txn);

        ASSERT_FALSE(res);
        EXPECT_EQ(req.type,req2.type);
        EXPECT_EQ(req.previous,req2.previous);
        EXPECT_EQ(req.next,req2.next);
        EXPECT_EQ(req.fee,req2.fee);
        EXPECT_EQ(req.sequence,req2.sequence);
        EXPECT_EQ(req.digest,req2.digest);
        ASSERT_EQ(req,req2);

        //ElectionVote no votes
        BlockHash prev = 111;
        AccountAddress address = 1;
        AccountSig sig = 1;
        Amount fee = 7;
        uint32_t sequence = 2;
        ElectionVote ev(address, prev, fee, sequence, sig);

        res = store->request_put(ev,ev.Hash(),txn);
        ASSERT_FALSE(res);

        ElectionVote ev2;
        auto hash = ev.Hash();
        res = store->request_get(hash,ev2,txn);
        ASSERT_FALSE(res);
        ASSERT_EQ(ev2.type,ev.type);
        ASSERT_EQ(ev2.previous,ev.previous);
        ASSERT_EQ(ev2.origin,ev.origin);
        ASSERT_EQ(ev2.signature,ev.signature);
        ASSERT_EQ(ev2.fee,ev.fee);
        ASSERT_EQ(ev2.sequence,ev.sequence);
        ASSERT_EQ(ev2.votes_,ev.votes_);
        ASSERT_EQ(ev2.digest,ev.digest);
        ASSERT_EQ(ev,ev2);

        //Election vote with 3 votes
        ElectionVote::CandidateVotePair p1(1,8);
        ElectionVote::CandidateVotePair p2(2,12);
        ElectionVote::CandidateVotePair p3(3,5);
        std::vector<ElectionVote::CandidateVotePair> votes = {p1,p2,p3};
        ev.votes_ = votes;
        ev.origin = 12;

        std::cout << "putting" << std::endl;
        hash = ev.Hash();
        res = store->request_put(ev,hash,txn);
        ASSERT_FALSE(res);

        ElectionVote ev3;
        std::cout << "size before serializing is " << ev.WireSize() << std::endl;
        res = store->request_get(ev.Hash(),ev3,txn);
        ASSERT_FALSE(res);
        ASSERT_EQ(ev3.type,ev.type);
        ASSERT_EQ(ev3.previous,ev.previous);
        ASSERT_EQ(ev3.origin,ev.origin);
        ASSERT_EQ(ev3.signature,ev.signature);
        ASSERT_EQ(ev3.fee,ev.fee);
        ASSERT_EQ(ev3.sequence,ev.sequence);
        ASSERT_EQ(ev3.votes_,ev.votes_);
        ASSERT_EQ(ev3.digest,ev.digest);
        ASSERT_EQ(ev,ev3);
        ASSERT_NE(ev3,ev2);


        RepInfo rep_info;
        AccountAddress rep_account = 1;
        rep_info.election_vote_tip = ev.Hash();

        res = store->rep_put(rep_account,rep_info,txn);
        ASSERT_FALSE(res);

        RepInfo rep_info2;
        res = store->rep_get(rep_account,rep_info2,txn);
        ASSERT_FALSE(res);
        ASSERT_EQ(rep_info,rep_info2);
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
    AccountAddress a1(0);
    CandidateInfo c2(false,false,110);
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
//        ASSERT_EQ(c3.votes_received_weighted,c1.votes_received_weighted+100+50);

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
    logos::transaction txn(store->environment,nullptr,true);

    size_t num_winners = 8;
    auto winners = getElectionWinners(num_winners,*store,txn);
    ASSERT_EQ(winners.size(),0);
    
    std::vector<std::pair<AccountAddress,Amount>> candidates;
    size_t num_candidates = 100;
    for(size_t i = 0; i < num_candidates; ++i)
    {
        CandidateInfo c(false,false,(i % 3) * 100 + i);
        AccountAddress a(i);
        store->candidate_put(a,c,txn);
        candidates.push_back(std::make_pair(a,c.votes_received_weighted));
    }
    std::sort(candidates.begin(), candidates.end(),
            [](auto p1, auto p2) 
            {
                return p1.second > p2.second;
            });

    std::vector<std::pair<AccountAddress,Amount>> results(
            candidates.begin(),candidates.begin() + num_winners);
    
    winners = getElectionWinners(num_winners,*store,txn);

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

TEST(Database,candidates_transition)
{
    logos::block_store* store = get_db();
    store->clear(store->candidacy_db);

    AccountAddress a1(0);
    AccountAddress a2(1);
    AccountAddress a3(2);

    logos::transaction txn(store->environment,nullptr,true);
    {
        bool res = store->candidate_add_new(a1,txn);
        ASSERT_FALSE(res);
        res = store->candidate_add_new(a2,txn);
        ASSERT_FALSE(res);
    }
    iterateCandidatesDB(*store,[](auto& it){
            bool error = false;
            CandidateInfo info(error,it->second);
            ASSERT_FALSE(error);
            ASSERT_FALSE(info.active);
            ASSERT_FALSE(info.remove);
            },txn);       

    updateCandidatesDB(*store,txn);

    iterateCandidatesDB(*store,[](auto& it){
            bool error = false;
            CandidateInfo info(error,it->second);
            ASSERT_FALSE(error);
            ASSERT_TRUE(info.active);
            ASSERT_FALSE(info.remove);
            },txn);

    {
        bool res = store->candidate_mark_remove(a1,txn);
        ASSERT_FALSE(res);
        CandidateInfo info;
        res = store->candidate_get(a1,info,txn);
        ASSERT_FALSE(res);
        ASSERT_TRUE(info.remove);
        ASSERT_TRUE(info.active);
        res = store->candidate_add_new(a3,txn);
        ASSERT_FALSE(res);
    }

    updateCandidatesDB(*store,txn);

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
    ASSERT_FALSE(transitionCandidatesDBNextEpoch(*store,txn));


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

        ASSERT_FALSE(transitionCandidatesDBNextEpoch(*store,txn));
        {
            CandidateInfo info;
            bool res = store->candidate_get(a2,info,txn);
            ASSERT_TRUE(res);
            eb.previous = eb.Hash();
            ASSERT_FALSE(store->epoch_put(eb,txn));
            ASSERT_FALSE(store->epoch_tip_put(eb.Hash(),txn));
        }
    }

    ASSERT_FALSE(transitionCandidatesDBNextEpoch(*store,txn));

    {
        CandidateInfo info;
        bool res = store->candidate_get(a2,info,txn);
        ASSERT_FALSE(res);
    }
}

TEST(Database,get_next_epoch_delegates)
{
    logos::block_store* store = get_db();
    store->clear(store->candidacy_db);
    store->clear(store->epoch_db);
    store->clear(store->epoch_tip_db);
    DelegateIdentityManager::_epoch_transition_enabled = true;

    uint32_t epoch_num = 0;
    ApprovedEB eb;
    eb.epoch_number = epoch_num;
    eb.previous = 0;
    EpochVotingManager mgr(*store);

    std::vector<Delegate> delegates;
    for(size_t i = 0; i < 32; ++i)
    {
        DelegatePubKey dummy_bls_pub;
        Amount dummy_stake = 1; 
        Delegate d(i,dummy_bls_pub,i,1);
        d.starting_term = true;
        eb.delegates[i] = d;
        delegates.push_back(d);
    }
    {
        logos::transaction txn(store->environment,nullptr,true);
        ASSERT_FALSE(store->epoch_tip_put(eb.Hash(),txn));
        ASSERT_FALSE(store->epoch_put(eb,txn));
    }

    auto transition_epoch = [&eb,&store,&epoch_num,&mgr](int retire_idx = -1)
    {
        std::cout << "transitioning" << std::endl;
        ++epoch_num;
        eb.previous = eb.Hash();
        eb.epoch_number = epoch_num;
        if(retire_idx >= 0)
        {
            std::unordered_set<Delegate> to_retire;
            size_t end = retire_idx + 8;
            for(;retire_idx < end; ++retire_idx)
            {
                to_retire.insert(eb.delegates[retire_idx]);
            }
            mgr.GetNextEpochDelegates(eb.delegates,&to_retire);
        }
        else
        {
            mgr.GetNextEpochDelegates(eb.delegates);
        }
        logos::transaction txn(store->environment,nullptr,true);
        ASSERT_FALSE(store->epoch_tip_put(eb.Hash(),txn));
        ASSERT_FALSE(store->epoch_put(eb,txn));
        if(epoch_num > 2)
        {
            ASSERT_FALSE(transitionCandidatesDBNextEpoch(*store,txn));
        }
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
            store->candidate_add_vote(delegates[i].account,100+i,txn);
            delegates[i].vote = 100+i; 
            delegates[i].starting_term = true;
        }
        std::sort(delegates.begin(),delegates.end(),[](auto d1, auto d2){
                return d1.vote < d2.vote;
                });
    }
    transition_epoch(0);
    compare_delegates();
    candidates = get_candidates();
    ASSERT_EQ(candidates.size(),24);
    {
        logos::transaction txn(store->environment,nullptr,true);
        for(size_t i = 0; i < 8; ++i)
        {
            store->candidate_add_vote(delegates[i].account,200+i,txn);
            delegates[i].vote = 200+i; 
            delegates[i].starting_term = true;
        }
        for(size_t i = 24; i < 32; ++i)
        {
            delegates[i].starting_term = false;
        }
        std::sort(delegates.begin(),delegates.end(),[](auto d1, auto d2){
                return d1.vote < d2.vote;
                });
    }
    transition_epoch(0);
    compare_delegates();
    candidates = get_candidates();
    ASSERT_EQ(candidates.size(),16);

    {
        logos::transaction txn(store->environment,nullptr,true);
        for(size_t i = 0; i < 8; ++i)
        {
            store->candidate_add_vote(delegates[i].account,300+i,txn);
            delegates[i].vote = 300+i; 
            delegates[i].starting_term = true;
        }
        for(size_t i = 24; i < 32; ++i)
        {
            delegates[i].starting_term = false;
        }
        std::sort(delegates.begin(),delegates.end(),[](auto d1, auto d2){
                return d1.vote < d2.vote;
                });
    }

    transition_epoch(0);
    compare_delegates();
    candidates = get_candidates();
    ASSERT_EQ(candidates.size(),8);


    {
        logos::transaction txn(store->environment,nullptr,true);
        for(size_t i = 0; i < 8; ++i)
        {
            store->candidate_add_vote(delegates[i].account,400+i,txn);
            delegates[i].vote = 400+i; 
            delegates[i].starting_term = true;
        }
        for(size_t i = 24; i < 32; ++i)
        {
            delegates[i].starting_term = false;
        }
        std::sort(delegates.begin(),delegates.end(),[](auto d1, auto d2){
                return d1.vote < d2.vote;
                });
    }

    transition_epoch(0);
    compare_delegates();


    auto retiring = mgr.GetRetiringDelegates();


//    for(auto it : retiring)
//    {
//        std::cout << it.account.to_string() << std::endl;
//        std::cout << it.vote.to_string() << std::endl;
//        std::cout << it.stake.to_string() << std::endl;
//        std::cout << it.starting_term << std::endl;
//    }
//
//    for(size_t i = 0; i < 8; ++i)
//    {
//        std::cout << delegates[i].account.to_string() << std::endl;
//        std::cout << delegates[i].vote.to_string() << std::endl;
//        std::cout << delegates[i].stake.to_string() << std::endl;
//        std::cout << delegates[i].starting_term << std::endl;
//    }
//
//    for(size_t i = 0; i < 8; ++i)
//    {
//        ASSERT_TRUE(retiring.find(delegates[i])!=retiring.end());
//    }
//

    for(size_t e = 0; e < 50; ++e)
    {
        std::cout << "******LOOP NUMBER " << e << std::endl;
    candidates = get_candidates();
    ASSERT_EQ(candidates.size(),8);
    ASSERT_EQ(mgr.GetRetiringDelegates().size(),8);
        {
            logos::transaction txn(store->environment,nullptr,true);
            for(size_t i = 0; i < 8; ++i)
            {
                auto vote = 500 + (e*100) + i;
                ASSERT_FALSE(store->candidate_add_vote(delegates[i].account,vote,txn));
                delegates[i].vote = vote; 
                delegates[i].starting_term = true;
            }
            for(size_t i = 24; i < 32; ++i)
            {
                delegates[i].starting_term = false;
            }
            std::sort(delegates.begin(),delegates.end(),[](auto d1, auto d2){
                    return d1.vote < d2.vote;
                    });
        }
    transition_epoch();
    compare_delegates();
    }





    


    
    
    

    
}

TEST(Database,validate)
{
    logos::block_store* store = get_db();
    store->clear(store->candidacy_db);
    store->clear(store->representative_db);
    store->clear(store->epoch_db);
    logos::transaction txn(store->environment,nullptr,true);

    DelegateIdentityManager::_epoch_transition_enabled = true;

    uint32_t epoch_num = 0;
    ElectionVote vote;
    AnnounceCandidacy announce;
    RenounceCandidacy renounce;
    StartRepresenting start_rep;
    StopRepresenting stop_rep;
    //no epoch block created yet, everything should fail
    ASSERT_FALSE(isValid(*store,vote,epoch_num,txn));
    ASSERT_FALSE(isValid(*store,announce,epoch_num,txn));
    ASSERT_FALSE(isValid(*store,renounce,epoch_num,txn));
    ASSERT_FALSE(isValid(*store,stop_rep,epoch_num,txn));
    ASSERT_FALSE(isValid(*store,start_rep,epoch_num,txn));

    ApprovedEB eb;
    eb.epoch_number = epoch_num;
    eb.previous = 0;
    ASSERT_FALSE(store->epoch_tip_put(eb.Hash(),txn));
    ASSERT_FALSE(store->epoch_put(eb,txn));

    //epoch block created, but only StartRepresenting should pass
    ASSERT_FALSE(isValid(*store,vote,epoch_num,txn));
    ASSERT_FALSE(isValid(*store,announce,epoch_num,txn));
    ASSERT_FALSE(isValid(*store,renounce,epoch_num,txn));
    ASSERT_FALSE(isValid(*store,stop_rep,epoch_num,txn));
    ASSERT_TRUE(isValid(*store,start_rep,epoch_num,txn));

    ASSERT_TRUE(applyRequest(*store,start_rep,epoch_num,txn));

    //all should fail
    ASSERT_FALSE(isValid(*store,vote,epoch_num,txn));
    ASSERT_FALSE(isValid(*store,announce,epoch_num,txn));
    ASSERT_FALSE(isValid(*store,renounce,epoch_num,txn));
    ASSERT_FALSE(isValid(*store,stop_rep,epoch_num,txn));
    ASSERT_FALSE(isValid(*store,start_rep,epoch_num,txn));
    
    auto transition_epoch = [&eb,&store,&txn,&epoch_num]()
    {
        ++epoch_num;
        eb.previous = eb.Hash();
        eb.epoch_number = epoch_num;
        ASSERT_FALSE(store->epoch_tip_put(eb.Hash(),txn));
        ASSERT_FALSE(store->epoch_put(eb,txn));
        ASSERT_FALSE(transitionCandidatesDBNextEpoch(*store,txn));
    };

    std::cout << "first transition" << std::endl;
    transition_epoch();

    ASSERT_TRUE(isValid(*store,vote,epoch_num,txn));
    ASSERT_TRUE(isValid(*store,announce,epoch_num,txn));
    ASSERT_FALSE(isValid(*store,renounce,epoch_num,txn));
    ASSERT_TRUE(isValid(*store,stop_rep,epoch_num,txn));
    ASSERT_FALSE(isValid(*store,start_rep,epoch_num,txn));


    ASSERT_TRUE(applyRequest(*store,vote,epoch_num,txn));
    ASSERT_FALSE(isValid(*store,vote,epoch_num,txn));
    ASSERT_TRUE(applyRequest(*store,announce,txn));
    ASSERT_FALSE(isValid(*store,announce,epoch_num,txn));
    ASSERT_FALSE(isValid(*store,renounce,epoch_num,txn));


    std::cout << "second transition" << std::endl;
    transition_epoch();

    CandidateInfo info;
    ASSERT_FALSE(store->candidate_get(announce.origin,info,txn));
    ASSERT_TRUE(info.active);
    ASSERT_FALSE(info.remove);

    ASSERT_TRUE(isValid(*store,vote,epoch_num,txn));
    ASSERT_TRUE(isValid(*store,renounce,epoch_num,txn));
    ASSERT_FALSE(isValid(*store,announce,epoch_num,txn));


    //TODO: actually elect someone, test if they are removed from candidates db
}

#endif
