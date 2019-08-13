#include <gtest/gtest.h>

#include <logos/unit_test/msg_validator_setup.hpp>
#include <logos/consensus/persistence/request/request_persistence.hpp>
#include <logos/consensus/persistence/reservations.hpp>

#define Unit_Test_Self_Send

#ifdef Unit_Test_Self_Send


TEST (Self_Send, self_send)
{
    bool error = false;
    boost::filesystem::path db_file("./test_db/unit_test_db.lmdb");
    logos::block_store* store = new logos::block_store (error, db_file);
    ASSERT_FALSE(error);
    clear_dbs();
    std::shared_ptr<Reservations> reservations (std::make_shared<ConsensusReservations>(*store));
    PersistenceManager<R> req_pm(*store, reservations);

    Amount fee = PersistenceManager<R>::MinTransactionFee(RequestType::Send);

    Amount initial_balance = fee * 100;

    AccountAddress account = 11;
    AccountAddress account2 = 34;
    logos::account_info info;
    logos::account_info info2;

    uint64_t timestamp = 0;
    {
        logos::transaction txn(store->environment, nullptr, true);
        info.SetBalance(initial_balance, 0, txn);
        info2.SetBalance(initial_balance, 0, txn);
        ASSERT_FALSE(store->account_put(account, info, txn));
        ASSERT_FALSE(store->account_put(account2, info2, txn));
    }
    std::shared_ptr<Send> send = std::make_shared<Send>();
    send->origin = account;
    send->AddTransaction(account2,5);
    send->AddTransaction(account, 3);
    send->fee = fee;
    send->Hash();

    {
        logos::transaction txn(store->environment, nullptr, true);

        logos::process_return result;
        ASSERT_TRUE(req_pm.ValidateRequest(send,0,result));
        req_pm.ApplyRequest(send,timestamp,0,txn);

        store->account_get(account, info, txn);
        store->account_get(account2, info2, txn);

        //Test that self send transaction didn't go through,
        //but regular transaction did
        ASSERT_EQ(info.GetBalance(), initial_balance - 5 - fee);
        ASSERT_EQ(info2.GetBalance(), initial_balance + 5);

    }

    //Make sure self send does not cause overflow
    Amount max_bal("340282366920938463463374607431768211455");
    AccountAddress account3 = 42;
    logos::account_info info3;
    {

        logos::transaction txn(store->environment, nullptr, true);
        info3.SetBalance(max_bal,0,txn);
        ASSERT_FALSE(store->account_put(account3,info3,txn));
        ASSERT_EQ(info3.GetBalance(),max_bal);
    }
    {
        logos::transaction txn(store->environment, nullptr, true);
        std::shared_ptr<Send> send2 = std::make_shared<Send>();
        send2->origin = account3;
        send2->AddTransaction(account3,100000);
        send2->fee = fee;
        send2->Hash();

        logos::process_return result;
        ASSERT_TRUE(req_pm.ValidateRequest(send2,0,result));
        req_pm.ApplyRequest(send2,timestamp,0,txn);

        store->account_get(account3, info, txn);
    }

    ASSERT_EQ(info.GetBalance(), max_bal-fee);
}

#endif // #ifdef Unit_Test_Self_Send
