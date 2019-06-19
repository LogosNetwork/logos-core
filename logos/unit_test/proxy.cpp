#include <gtest/gtest.h>
#include <logos/governance/requests.hpp>
#include <logos/unit_test/msg_validator_setup.hpp>

#define Unit_Test_Proxy

#ifdef Unit_Test_Proxy

TEST(Proxy, Serialization)
{
    logos::block_store* store(get_db());
    logos::transaction txn(store->environment, nullptr, true);

    store->clear(store->state_db, txn);

    Proxy req;
    req.lock_proxy = 4267;
    req.rep = 1234;
    req.epoch_num = 720;
    req.governance_subchain_prev = 89674;

    req.Hash();
    store->request_put(req, txn);

    Proxy req2;
    ASSERT_FALSE(store->request_get(req.Hash(), req2, txn));
    ASSERT_EQ(req, req2);

    auto json = req.SerializeJson();

    bool error = false;
    Proxy req3(error, json);
    ASSERT_FALSE(error);
    ASSERT_EQ(req3, req);
}

#endif
