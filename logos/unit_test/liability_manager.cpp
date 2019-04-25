#include <gtest/gtest.h>
#include <logos/staking/liability_manager.hpp>
#include <logos/unit_test/msg_validator_setup.hpp>

#define Unit_Test_Liabilities

#ifdef Unit_Test_Liabilities


TEST(Liabilities, liabilities)
{
    logos::block_store* store(get_db());
    logos::transaction txn(store->environment, nullptr, true);
    store->clear(store->master_liabilities_db,txn);
    store->clear(store->secondary_liabilities_db,txn);
    store->clear(store->rep_liabilities_db,txn);

    AccountAddress origin = 67;
    AccountAddress rep = 23;
    Amount amount = 1000;
    uint32_t exp_epoch = 121;

    LiabilityManager liability_mgr(*store);
    std::vector<LiabilityHash> hashes;
    hashes = liability_mgr.GetRepLiabilities(rep, txn);
    ASSERT_EQ(hashes.size(), 0);
    hashes = liability_mgr.GetSecondaryLiabilities(origin, txn);
    ASSERT_EQ(hashes.size(), 0);

    auto hash = liability_mgr.CreateExpiringLiability(rep, origin, amount, exp_epoch, txn);

    ASSERT_TRUE(liability_mgr.Exists(hash, txn));

    hashes = liability_mgr.GetRepLiabilities(rep, txn);
    ASSERT_EQ(hashes.size(), 1);
    ASSERT_EQ(hashes[0],hash);
    hashes = liability_mgr.GetSecondaryLiabilities(origin, txn);
    ASSERT_EQ(hashes.size(), 0);
    Liability l = liability_mgr.Get(hash,txn);
    ASSERT_EQ(l.target,rep);
    ASSERT_EQ(l.source,origin);
    ASSERT_EQ(l.amount,amount);
    ASSERT_EQ(l.expiration_epoch, exp_epoch);

    amount += 200;
    liability_mgr.UpdateLiabilityAmount(hash,amount,txn);

    ASSERT_TRUE(liability_mgr.Exists(hash, txn));

    hashes = liability_mgr.GetRepLiabilities(rep, txn);
    ASSERT_EQ(hashes.size(), 1);
    ASSERT_EQ(hashes[0],hash);
    hashes = liability_mgr.GetSecondaryLiabilities(origin, txn);
    ASSERT_EQ(hashes.size(), 0);
    l = liability_mgr.Get(hash,txn);
    ASSERT_EQ(l.target,rep);
    ASSERT_EQ(l.source,origin);
    ASSERT_EQ(l.amount,amount);
    ASSERT_EQ(l.expiration_epoch, exp_epoch);

    Amount amount2 = 500;

    auto hash2 = liability_mgr.CreateUnexpiringLiability(rep,origin,amount2,txn);

    ASSERT_TRUE(liability_mgr.Exists(hash, txn));

    hashes = liability_mgr.GetRepLiabilities(rep, txn);
    ASSERT_EQ(hashes.size(), 2);
    ASSERT_EQ(hashes[0],hash);
    ASSERT_EQ(hashes[1],hash2);
    hashes = liability_mgr.GetSecondaryLiabilities(origin, txn);
    ASSERT_EQ(hashes.size(), 0);
    l = liability_mgr.Get(hash,txn);
    ASSERT_EQ(l.target,rep);
    ASSERT_EQ(l.source,origin);
    ASSERT_EQ(l.amount,amount);
    ASSERT_EQ(l.expiration_epoch, exp_epoch);

    l = liability_mgr.Get(hash2,txn);
    ASSERT_EQ(l.target,rep);
    ASSERT_EQ(l.source,origin);
    ASSERT_EQ(l.amount,amount2);
    ASSERT_EQ(l.expiration_epoch, 0);

    Amount amount3 = 400;

    auto hash3 = liability_mgr.CreateExpiringLiability(rep, origin, amount3, exp_epoch, txn);
    ASSERT_EQ(hash3,hash);
    hashes = liability_mgr.GetRepLiabilities(rep, txn);
    ASSERT_EQ(hashes.size(), 2);
    ASSERT_EQ(hashes[0],hash);
    ASSERT_EQ(hashes[1],hash2);
    hashes = liability_mgr.GetSecondaryLiabilities(origin, txn);
    ASSERT_EQ(hashes.size(), 0);
    l = liability_mgr.Get(hash,txn);
    ASSERT_EQ(l.target,rep);
    ASSERT_EQ(l.source,origin);
    ASSERT_EQ(l.amount,amount+amount3);
    ASSERT_EQ(l.expiration_epoch, exp_epoch);

    l = liability_mgr.Get(hash2,txn);
    ASSERT_EQ(l.target,rep);
    ASSERT_EQ(l.source,origin);
    ASSERT_EQ(l.amount,amount2);
    ASSERT_EQ(l.expiration_epoch, 0);


    ++exp_epoch;
    Amount amount4 = 2000;
    auto hash4 = liability_mgr.CreateExpiringLiability(rep, origin, amount4, exp_epoch, txn);
    ASSERT_NE(hash3,hash4);
    hashes = liability_mgr.GetRepLiabilities(rep, txn);
    ASSERT_EQ(hashes.size(), 3);
    ASSERT_EQ(hashes[0],hash);
    ASSERT_EQ(hashes[1],hash4);
    ASSERT_EQ(hashes[2],hash2);
    hashes = liability_mgr.GetSecondaryLiabilities(origin, txn);
    ASSERT_EQ(hashes.size(), 0);
    l = liability_mgr.Get(hash,txn);
    ASSERT_EQ(l.target,rep);
    ASSERT_EQ(l.source,origin);
    ASSERT_EQ(l.amount,amount+amount3);
    ASSERT_EQ(l.expiration_epoch, exp_epoch-1);

    l = liability_mgr.Get(hash2,txn);
    ASSERT_EQ(l.target,rep);
    ASSERT_EQ(l.source,origin);
    ASSERT_EQ(l.amount,amount2);
    ASSERT_EQ(l.expiration_epoch, 0);

    l = liability_mgr.Get(hash4,txn);
    ASSERT_EQ(l.target,rep);
    ASSERT_EQ(l.source,origin);
    ASSERT_EQ(l.amount,amount4);
    ASSERT_EQ(l.expiration_epoch, exp_epoch);







}

#endif
