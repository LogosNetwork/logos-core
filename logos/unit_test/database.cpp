
#include <gtest/gtest.h>
#include <logos/blockstore.hpp>
#include <logos/unit_test/msg_validator_setup.hpp>
#include <logos/epoch/election_requests.hpp>
#include <logos/common.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
#include <logos/elections/database.hpp>
#include <logos/elections/database_functions.hpp>

#define Unit_Test_Database

#ifdef Unit_Test_Database


//candidates are passed in outcome order)
void generateReps(
        size_t num_reps,
        const std::vector<AccountAddress>& candidates,
        logos::block_store* store)
{
    logos::transaction txn(store->environment,nullptr,true);
    for(size_t i = 0; i < num_reps; ++i)
    {
       ElectionVote ev;
       for(size_t j = 0; j < candidates.size(); ++j)
       {
            ev.votes_.emplace_back(candidates[j],candidates.size()-j);
       } 
       bool res = store->request_put(ev,ev.Hash(),txn);
       ASSERT_FALSE(res);
       RepInfo rep_info;
       AccountAddress rep_account = i;
       rep_info.election_vote_tip = ev.Hash();
       res = store->rep_put(rep_account,rep_info,txn);
       ASSERT_FALSE(res);
    }
}

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
   
    { 
    //voting with one rep
    EpochVotingManager mgr(*store);

    std::vector<Delegate> de(mgr.GetDelegateElects(1));

    ASSERT_EQ(de.size(),1);
    ASSERT_EQ(de[0].account,2);

    de = mgr.GetDelegateElects(3);
    ASSERT_EQ(de.size(),3);
    ASSERT_EQ(de[0].account,2);
    ASSERT_EQ(de[1].account,1);
    ASSERT_EQ(de[2].account,3);
    }
    //add some more reps to db
    {

        std::vector<AccountAddress> candidates{100,101,102,103,104,105,106,107};
        generateReps(100,candidates,store);
        EpochVotingManager mgr(*store);
        std::vector<Delegate> de(mgr.GetDelegateElects(4));
        ASSERT_EQ(de.size(), 4);
        ASSERT_EQ(de[0].account,candidates[0]);
        ASSERT_EQ(de[1].account,candidates[1]);
        ASSERT_EQ(de[2].account,candidates[2]);
        ASSERT_EQ(de[3].account,candidates[3]);
    }
}


void computeWinners(
        size_t num_candidates,
        size_t num_winners,
        std::function<uint64_t(size_t i)> votes_func,
        logos::block_store& store,
        MDB_txn* txn)
{
    store.clear(store.candidacy_db,txn);
    using CandidatePair = std::pair<AccountAddress,CandidateInfo>;
    auto hash_func = [](const CandidatePair& c) {
        return std::hash<AccountAddress>()(c.first) ^ 
            std::hash<uint64_t>()(c.second.votes_received_weighted);
    };

    std::unordered_set<CandidatePair,decltype(hash_func)> 
        candidates(num_candidates,hash_func);
    {

        for(size_t i = 0; i < num_candidates; ++i)
        {
            CandidateInfo c(true,false,votes_func(i));
            AccountAddress a(i);
            bool res = store.candidate_put(a,c,txn);
            ASSERT_FALSE(res);
            candidates.insert(std::make_pair(a,c));
        }

        std::unordered_set<CandidatePair,decltype(hash_func)> 
            candidates_from_db(num_candidates,hash_func);
        for(auto it = logos::store_iterator(txn, store.candidacy_db);
                it != logos::store_iterator(nullptr); ++it)
        {
            AccountAddress a(it->first.uint256());
            bool res = false;
            CandidateInfo c(res,it->second);
            ASSERT_FALSE(res);
            candidates_from_db.insert(std::make_pair(a,c));
        }
        ASSERT_EQ(candidates,candidates_from_db);
    }

    auto results = getElectionWinners(num_winners,store,txn);

    std::vector<CandidatePair> res_exp(candidates.begin(),candidates.end());

    std::sort(res_exp.begin(),res_exp.end(),[](auto& p1,auto& p2){
            return p1.second.votes_received_weighted > p2.second.votes_received_weighted;});

    ASSERT_EQ(results.size(),num_winners);

    for(size_t i = 0; i < num_winners; ++i)
    {
        ASSERT_EQ(results[i].first,res_exp[i].first);
        ASSERT_EQ(results[i].second,res_exp[i].second.votes_received_weighted);
    } 
}


TEST(database, heap)
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


TEST(database, candidates)
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

        res = store->candidate_get(a1,c1_copy,txn);
        ASSERT_FALSE(res);
        ASSERT_EQ(c1_copy.votes_received_weighted,c1.votes_received_weighted+100+50);
        
        res = store->candidate_add_vote(a2,100,txn);
        ASSERT_TRUE(res);

        AccountAddress a3(2);
        res = store->candidate_add_vote(a3,100,txn);
        ASSERT_TRUE(res);
        
    }

    std::cout << "trying to compute winners" << std::endl;
    computeWinners(25,8,[](size_t i){ return i;},*store,txn);

    computeWinners(100,8,[](size_t i){ return i + (100 * i%3);},*store,txn);




    //TODO: write tests for candidate functions
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

TEST(database,candidates_transition)
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

TEST(database,validate)
{
    logos::block_store* store = get_db();
    store->clear(store->candidacy_db);
    store->clear(store->representative_db);
    store->clear(store->epoch_db);
    logos::transaction txn(store->environment,nullptr,true);

    uint32_t epoch_num;
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
