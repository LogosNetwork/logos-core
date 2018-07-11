#pragma once

#include <rai/consensus/messages/messages.hpp>
#include <rai/common.hpp>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>

class MessageValidator
{

    using Log       = boost::log::sources::logger_mt;
    using PublicKey = rai::public_key;
    using Keys      = std::unordered_map<uint8_t, PublicKey>;
    using KeyPair   = rai::keypair;

public:

    struct DelegateSignature
    {
        uint8_t   delegate_id;
        Signature signature;
    };

    // Aggregate sign
    template<typename MSG>
    void Sign(MSG & message, const std::vector<DelegateSignature> & signatures)
    {
        // Set participation map:
        for(auto & sig : signatures)
        {
            message.participation_map[sig.delegate_id] = true;
        }

        // Now sign the message with the aggregate sig
    }

    // Single sign
    template<typename MSG>
    void Sign(MSG & message)
    {}

    // Aggregate validation
    template<MessageType type>
    bool Validate(const PostPhaseMessage<type> & message)
    {
        // Use message.participation_map
        // to identify those delegates that
        // signed, and access their public
        // keys using _keys.
        return true;
    }

    // Single validation
    template<MessageType type>
    bool Validate(const StandardPhaseMessage<type> & message)
    {
        return true;
    }

    void OnPublicKey(uint8_t delegate_id, const PublicKey & key);

    PublicKey GetPublicKey();

private:

    Log     _log;
    Keys    _keys;
    KeyPair _keypair;
};


