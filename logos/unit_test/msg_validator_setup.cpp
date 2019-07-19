#include <logos/unit_test/msg_validator_setup.hpp>

using namespace std;
BLS_Nodes & setup_nodes()
{
    static std::mutex setup_nodes_mutex;
    static bool inited = false;
    static BLS_Nodes bls_nodes;

    std::lock_guard<std::mutex> lock(setup_nodes_mutex);
    if(! inited )
    {
        vector<DelegatePubKey> pkeys;
        for(int i = 0; i < NUM_DELEGATES; ++i)
        {
            bls_nodes.push_back(new BLS_Node());
            pkeys.push_back(bls_nodes[i]->validator.GetPublicKey());
        }

        // everyone gets everyone's public key, including itself's
        for(int i = 0; i < NUM_DELEGATES; ++i)
        {
            auto & ks = bls_nodes[i]->validator.keyStore;
            for(int k = 0; k < NUM_DELEGATES; ++k)
            {
                ks.OnPublicKey(k, pkeys[k]);
            }
        }
        inited = true;
    }
    return bls_nodes;
}

logos::block_store * get_db()
{
    static std::mutex setup_nodes_mutex;
    static bool inited = false;
    static logos::block_store *store = NULL;

    std::lock_guard<std::mutex> lock(setup_nodes_mutex);
    if(! inited )
    {
        bool error = false;
        boost::filesystem::path db_file("./test_db/unit_test_db.lmdb");
        store = new logos::block_store (error, db_file);
        if(error)
            store = NULL;
        inited = true;
    }


    return store;
}

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
    store->clear(store->voting_power_db);
    store->clear(store->staking_db);
    store->clear(store->thawing_db);
    store->clear(store->master_liabilities_db);
    store->clear(store->secondary_liabilities_db);
    store->clear(store->rep_liabilities_db);
    store->clear(store->rewards_db);
    store->clear(store->global_rewards_db);
    store->clear(store->delegate_rewards_db);
    store->clear(store->account_db);
    store->leading_candidates_size = 0;
}
