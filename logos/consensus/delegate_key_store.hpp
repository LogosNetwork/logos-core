#pragma once

#include <unordered_map>
#include <mutex>

#include <logos/consensus/messages/common.hpp>
#include <logos/lib/log.hpp>

#include <bls/bls.hpp>

class DelegateKeyStore
{
    using PublicKeyReal = bls::PublicKey;
    using Keys          = std::unordered_map<uint8_t, PublicKeyReal>;

public:

    bool OnPublicKey(uint8_t delegate_id, const PublicKey & key);

    PublicKeyReal GetPublicKey(uint8_t delegate_id);
    PublicKeyReal GetAggregatedPublicKey(const ParicipationMap &pmap);

private:

    Log        _log;
    Keys       _keys;
    std::mutex _mutex;
};
