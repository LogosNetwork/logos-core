#include <logos/consensus/delegate_key_store.hpp>

using PublicKeyReal  = bls::PublicKey;
using PublicKeyVec   = bls::PublicKeyVec;

bool DelegateKeyStore::OnPublicKey(uint8_t delegate_id, const PublicKey & key)
{
    std::string keystring(reinterpret_cast<const char*>(&key), CONSENSUS_PUB_KEY_SIZE);
    PublicKeyReal k;

    try
    {
        k.deserialize(keystring);
    }
    catch (bls::Exception &e)
    {
        LOG_ERROR(_log) << "DelegateKeyStore::OnPublicKey failed to deserialize the public key of delegate " << (int)delegate_id;
        return false;
    }

//    BOOST_LOG (_log) << "DelegateKeyStore - Received public key: "
//                     << k.to_string()
//                     << " from delegate "
//                     << (int)delegate_id;

    std::lock_guard<std::mutex> lock(_mutex);
    if(_keys.find(delegate_id) != _keys.end())
    {
        LOG_WARN(_log) << "DelegateKeyStore::OnPublicKey already have the public key of delegate " << (int)delegate_id;
        return false;
    }

    _keys[delegate_id] = k;

    return true;
}

PublicKeyReal DelegateKeyStore::GetPublicKey(uint8_t delegate_id)
{
    if(_keys.find(delegate_id) == _keys.end())
    {
        LOG_WARN(_log) << "DelegateKeyStore::GetPublicKey don't have the public key of delegate " << (int)delegate_id;
        return PublicKeyReal();
    }

    return _keys[delegate_id];
}

PublicKeyReal DelegateKeyStore::GetAggregatedPublicKey(const ParicipationMap &pmap)
{
    PublicKeyVec keyvec;
    for(int i = 0; i < pmap.size(); ++i)
    {
        if(pmap[i])
        {
            if(_keys.find(i) == _keys.end())
            {
                LOG_WARN(_log) << "DelegateKeyStore::GetAggregatedPublicKey don't have the public key of delegate " << i;
                return PublicKeyReal();
            }
            keyvec.push_back(GetPublicKey(i));
        }
    }

    PublicKeyReal apk;
    apk.aggregateFrom(keyvec);

    return apk;
}
