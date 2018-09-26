#pragma once

#include <unordered_map>
#include <mutex>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>

#include <logos/consensus/messages/common.hpp>
#include <bls/bls.hpp>

class DelegateKeyStore
{
    using Log           = boost::log::sources::logger_mt;
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
