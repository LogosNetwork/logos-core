#pragma once

#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/messages/common.hpp>
#include <logos/common.hpp>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>

#include <bls/bls.hpp>

class MessageValidator
{
    using Log           = boost::log::sources::logger_mt;
    using KeyPair       = bls::KeyPair;
    using PublicKeyReal = bls::PublicKey;
    using PublicKeyVec  = bls::PublicKeyVec;
    using SignatureReal = bls::Signature;
    using SignatureVec  = bls::SignatureVec;

public:

    struct DelegateSignature
    {
        uint8_t   delegate_id;
        Signature signature;
    };

    MessageValidator(DelegateKeyStore & key_store);

    // Aggregate sign
    template<typename MSG>
    bool Sign(MSG & message, const std::vector<DelegateSignature> & signatures)
    {
        PublicKeyVec keyvec;
        SignatureVec sigvec;

        for(auto & sig : signatures)
        {
            message.participation_map[sig.delegate_id] = true;

            SignatureReal sigReal;
            string sig_str(reinterpret_cast<const char*>(&sig.signature), CONSENSUS_SIG_SIZE);
            try
            {
                sigReal.deserialize(sig_str);
            }
            catch(bls::Exception &e)
            {
                BOOST_LOG (_log) << "MessageValidator - Aggregate sign, failed to deserialize a signature";
                return false;
            }
            sigvec.push_back(sigReal);

            keyvec.push_back(_keys.GetPublicKey(sig.delegate_id));
        }

        // Now aggregate sig
        SignatureReal asig;
        asig.aggregateFrom(sigvec, keyvec);

        string asig_str;
        asig.serialize(asig_str);
        memcpy(&message.signature, asig_str.data(), CONSENSUS_SIG_SIZE);
        return true;
    }

    // Single sign
    template<typename MSG>
    void Sign(MSG & message)
    {
        string msg(reinterpret_cast<const char*>(&message), MSG::HASHABLE_BYTES);

        SignatureReal sig;
        _keypair.prv.sign(sig, msg);

        string sig_str;
        sig.serialize(sig_str);
        memcpy(&message.signature, sig_str.data(), CONSENSUS_SIG_SIZE);
    }

    // Aggregate validation
    template<MessageType type, MessageType type2, ConsensusType consensus_type>
    bool Validate(const PostPhaseMessage<type, consensus_type> & message, const StandardPhaseMessage<type2, consensus_type> & reference)
    {
        if (message.participation_map.none())
        {
            BOOST_LOG (_log) << "MessageValidator - Aggregate validate, empty participation_map";
            return false;
        }

        //public key agg
        auto apk = _keys.GetAggregatedPublicKey(message.participation_map);

        //message
        string msg(reinterpret_cast<const char*>(&reference), StandardPhaseMessage<type2, consensus_type>::HASHABLE_BYTES);

        //deserialize sig
        SignatureReal sig;
        string sig_str(reinterpret_cast<const char*>(&message.signature), CONSENSUS_SIG_SIZE);

        try
        {
            sig.deserialize(sig_str);
        }
        catch (const bls::Exception &)
        {
            BOOST_LOG (_log) << "MessageValidator - Aggregate validate, failed to deserialize the signature";
            return false;
        }

        //verify
        return sig.verify(apk, msg);
    }

    // Single validation.
    //
    // TODO: Validate PrePrepare messages
    //
    template<typename MSG>
    bool Validate(const MSG & message, uint8_t delegate_id)
    {

        //message
        string msg(reinterpret_cast<const char*>(&message), MSG::HASHABLE_BYTES);

        //deserialize sig
        SignatureReal sig;
        string sig_str(reinterpret_cast<const char*>(&message.signature), CONSENSUS_SIG_SIZE);

        try
        {
            sig.deserialize(sig_str);
        }
        catch (const bls::Exception &)
        {
            return false;
        }

        //verify
        return sig.verify(_keys.GetPublicKey(delegate_id), msg);
    }

    PublicKey GetPublicKey();

private:

    Log                 _log;
    KeyPair             _keypair;
    DelegateKeyStore &  _keys;
};
