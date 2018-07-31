#include <rai/consensus/message_validator.hpp>
#include <string>

MessageValidator::MessageValidator(uint8_t my_delegate_id, DelegateKeyStore & key_store)
    : _my_delegate_id(my_delegate_id)
    , _keys(key_store)
{}

void MessageValidator::UpdateMyId(uint8_t my_delegate_id)
{
    _my_delegate_id = my_delegate_id;
}

PublicKey MessageValidator::GetPublicKey()
{

    std::string keystring;
    _keypair.pub.serialize(keystring);

    PublicKey pk;
    memcpy(&pk[0], keystring.data(), CONSENSUS_PUB_KEY_SIZE);

    return pk;
}
