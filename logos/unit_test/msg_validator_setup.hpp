#pragma once

#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/blockstore.hpp>

#include <vector>

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

using BLS_Nodes = std::vector<BLS_Node *>;

using SigVec = std::vector<MessageValidator::DelegateSignature>;

BLS_Nodes & setup_nodes();

logos::block_store * get_db();


