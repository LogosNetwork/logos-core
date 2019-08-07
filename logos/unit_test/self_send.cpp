#include <gtest/gtest.h>

#include <logos/unit_test/msg_validator_setup.hpp>
#include <logos/consensus/persistence/request/request_persistence.hpp>
#include <logos/consensus/persistence/reservations.hpp>

#define Unit_Test_Self_Send

#ifdef Unit_Test_Self_Send


TEST (Self_Send, self_send)
{
    logos::block_store* store = get_db();
    clear_dbs();
    std::shared_ptr<Reservations> reservations (std::make_shared<ConsensusReservations>(*store));
    PersistenceManager<R> req_pm(*store, reservations);


    Amount initial_balance = 100;

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


    {
        
    logos::transaction txn(store->environment, nullptr, true);
    req_pm.ApplyRequest(send,timestamp,0,txn);

    store->account_get(account, info, txn);
    store->account_get(account2, info2, txn);

    //Test that self send transaction didn't go through,
    //but regular transaction did
    ASSERT_EQ(info.GetBalance(), initial_balance - 5 );
    ASSERT_EQ(info2.GetBalance(), initial_balance + 5);

    }
    //Make sure self send does not cause overflow
    Amount max_bal("340282366920938463463374607431768211455");

    AccountAddress account3 = 42;
    {

    logos::transaction txn(store->environment, nullptr, true);
    info.SetBalance(max_bal,0,txn);
    ASSERT_FALSE(store->account_put(account3,info,txn));
    }
    {
    logos::transaction txn(store->environment, nullptr, true);
    std::shared_ptr<Send> send2 = std::make_shared<Send>();
    send2->origin = account3;
    send2->AddTransaction(account3,100000);
    req_pm.ApplyRequest(send,timestamp,0,txn);

    store->account_get(account3, info, txn);
    }

    ASSERT_EQ(info.GetBalance(), max_bal);
}

#endif // #ifdef Unit_Test_Self_Send
