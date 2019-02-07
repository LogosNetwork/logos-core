
#include <gtest/gtest.h>
#include <logos/blockstore.hpp>
#include <logos/unit_test/msg_validator_setup.hpp>
#include <logos/epoch/election_requests.hpp>
#include <logos/common.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>

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
       logos::RepInfo rep_info;
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


        logos::RepInfo rep_info;
        AccountAddress rep_account = 1;
        rep_info.election_vote_tip = ev.Hash();

        res = store->rep_put(rep_account,rep_info,txn);
        ASSERT_FALSE(res);

        logos::RepInfo rep_info2;
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

        std::cout << "big test" << std::endl;
        std::vector<AccountAddress> candidates{100,101,102,103,104,105,106,107};
        generateReps(100000,candidates,store);
        std::cout << "wrote reps" << std::endl; 
        EpochVotingManager mgr(*store);
        std::vector<Delegate> de(mgr.GetDelegateElects(4));
        ASSERT_EQ(de.size(), 4);
        ASSERT_EQ(de[0].account,candidates[0]);
        ASSERT_EQ(de[1].account,candidates[1]);
        ASSERT_EQ(de[2].account,candidates[2]);
        ASSERT_EQ(de[3].account,candidates[3]);
    }

}

#endif
