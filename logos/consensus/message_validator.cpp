#include <logos/consensus/message_validator.hpp>
#include <string>

MessageValidator::MessageValidator(DelegateKeyStore & key_store)
    : _keys(key_store)
{
}

PublicKey MessageValidator::GetPublicKey()
{
    std::string keystring;
    _keypair.pub.serialize(keystring);

    PublicKey pk;
    memcpy(&pk[0], keystring.data(), CONSENSUS_PUB_KEY_SIZE);

    return pk;
}
