#include <rai/consensus/message_validator.hpp>
#include <string>

void MessageValidator::Init(uint8_t my_delegate_id)
{
    _keys[my_delegate_id] = _keypair.pub;
}

void MessageValidator::OnPublicKey(uint8_t delegate_id, const PublicKey & key)  throw(bls::Exception)
{
    std::lock_guard<std::mutex> lock(_mutex);

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
    std::lock_guard<std::mutex> lock(_mutex);

	std::string keystring;
    _keypair.pub.serialize(keystring);

    PublicKey pk;
    memcpy(&pk[0], keystring.data(), CONSENSUS_PUB_KEY_SIZE);

    return pk;
}

MessageValidator::PublicKeyReal
MessageValidator::PublicKeyAggregate(const ParicipationMap &pmap)
{
    PublicKeyVec keyvec;

    for(int i = 0; i < pmap.size(); ++i)
    {
        if(pmap[i])
        {
            keyvec.push_back(_keys[i]);
        }
    }

    PublicKeyReal apk;

    apk.aggregateFrom(keyvec);
    return apk;
}
