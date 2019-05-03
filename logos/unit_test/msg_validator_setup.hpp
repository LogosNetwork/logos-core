#pragma once

#include <logos/identity_management/delegate_identity_manager.hpp>
#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/blockstore.hpp>

#include <vector>

class MessageValidatorTest : public MessageValidator
{
public:
    MessageValidatorTest(DelegateKeyStore &key_store) : MessageValidator(key_store) {}
    ~MessageValidatorTest() = default;

    void Sign(const BlockHash & hash, DelegateSig & sig) override
    {
        MessageValidator::Sign(hash, sig, [this](bls::Signature &sig_real, const std::string &hash_str) {
            key_pair.prv.sign(sig_real, hash_str);
        });
    }

    DelegatePubKey GetPublicKey() override
    {
        return BlsPublicKey(key_pair.pub);
    }

private:
    bls::KeyPair key_pair;
};

struct BLS_Node
{
    DelegateKeyStore key_store;
    bls::KeyPair key_pair;
    MessageValidatorTest validator;

    BLS_Node()
    : key_store()
    , key_pair()
    , validator(key_store)
    {}
};

using BLS_Nodes = std::vector<BLS_Node *>;

using SigVec = std::vector<MessageValidator::DelegateSignature>;

BLS_Nodes & setup_nodes();

logos::block_store * get_db();


