#pragma once

#include <rai/common.hpp>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>

class MessageValidator
{

    using Log       = boost::log::sources::logger_mt;
    using PublicKey = rai::public_key;
    using Keys      = std::vector<PublicKey>;
    using KeyPair   = rai::keypair;

public:

    template<typename MSG>
    void Sign(MSG & message)
    {}

    template<typename MSG>
    bool Validate(const MSG & message)
    {
        return true;
    }

    void OnPublicKey(const PublicKey & key)
    {
        BOOST_LOG (_log) << "MessageValidator - Received public key: "
                         << key.to_string();

        _keys.push_back(key);
    }

    PublicKey GetPublicKey()
    {
        return _keypair.pub;
    }

private:

    Log     _log;
    Keys    _keys;
    KeyPair _keypair;
};


