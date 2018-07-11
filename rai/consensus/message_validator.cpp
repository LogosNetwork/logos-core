#include <rai/consensus/message_validator.hpp>

void MessageValidator::OnPublicKey(uint8_t delegate_id, const PublicKey & key)
{
    BOOST_LOG (_log) << "MessageValidator - Received public key: "
                     << key.to_string()
                     << " from delegate "
                     << delegate_id;

    _keys[delegate_id] = key;
}

PublicKey MessageValidator::GetPublicKey()
{
    return _keypair.pub;
}
