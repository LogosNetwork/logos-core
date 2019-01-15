#include <logos/consensus/message_validator.hpp>
#include <string>

MessageValidator::MessageValidator(DelegateKeyStore & key_store, KeyPair & key_pair)
    : _keys(key_store)
    , _keypair(key_pair)
{
}

DelegatePubKey MessageValidator::GetPublicKey()
{
    std::string keystring;
    _keypair.pub.serialize(keystring);

    DelegatePubKey pk;
    memcpy(pk.data(), keystring.data(), CONSENSUS_PUB_KEY_SIZE);

    return pk;
}
