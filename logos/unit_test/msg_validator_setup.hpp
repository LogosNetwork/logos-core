#ifndef LOGOS_UNIT_TEST_MSG_VALIDATOR_SETUP_HPP_
#define LOGOS_UNIT_TEST_MSG_VALIDATOR_SETUP_HPP_

#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/blockstore.hpp>

#include <vector>
using namespace std;

struct BLS_Node
{
    DelegateKeyStore key_store;
    bls::KeyPair key_pair;
    MessageValidator validator;

    BLS_Node()
    : key_store()
    , key_pair()
    , validator(key_store, key_pair)
    {}
};

using BLS_Nodes = vector<BLS_Node *>;
using SigVec = vector<MessageValidator::DelegateSignature>;

BLS_Nodes & setup_nodes()
{
    static std::mutex setup_nodes_mutex;
    static bool inited = false;
    static BLS_Nodes bls_nodes;

    std::lock_guard<std::mutex> lock(setup_nodes_mutex);
    if(! inited )
    {
        bls::init();

        vector<DelegatePubKey> pkeys;
        for(int i = 0; i < NUM_DELEGATES; ++i)
        {
            bls_nodes.push_back(new BLS_Node());
            pkeys.push_back(bls_nodes[i]->validator.GetPublicKey());
        }

        // everyone gets everyone's public key, including itself's
        for(int i = 0; i < NUM_DELEGATES; ++i)
        {
            auto & ks = bls_nodes[i]->key_store;
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

#endif /* LOGOS_UNIT_TEST_MSG_VALIDATOR_SETUP_HPP_ */
