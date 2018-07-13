#include <rai/consensus/message_validator.hpp>
#include <string>

void MessageValidator::OnPublicKey(uint8_t delegate_id, const PublicKey & key)  throw(bls::Exception)
{
	std::string keystring(reinterpret_cast<const char*>(&key), CONSENSUS_PUB_KEY_SIZE);
	PublicKeyReal k;
	k.deserialize(keystring);

    BOOST_LOG (_log) << "MessageValidator - Received public key: "
                     << k.to_string()
                     << " from delegate "
                     << (int)delegate_id;

    _keys[delegate_id] = k;
}

PublicKey MessageValidator::GetPublicKey()
{
	std::string keystring;
    _keypair.pub.serialize(keystring);
    PublicKey pk;
    memcpy(&pk[0], keystring.data(), CONSENSUS_PUB_KEY_SIZE);

    return pk;
}
