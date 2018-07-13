#pragma once

#include <rai/consensus/messages/messages.hpp>
#include <rai/common.hpp>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>

#include <bls/bls.hpp>
#include "messages/common.hpp"


class MessageValidator
{
    using Log            = boost::log::sources::logger_mt;
    using KeyPair        = bls::KeyPair;
    using PublicKeyReal  = bls::PublicKey;
    using PublicKeyVec   = bls::PublicKeyVec;
    using SignatureReal  = bls::Signature;
    using SignatureVec   = bls::SignatureVec;
    using Keys           = std::unordered_map<uint8_t, PublicKeyReal>;

    PublicKeyReal PublicKeyAggregate(const ParicipationMap &pmap)
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

public:

    struct DelegateSignature
    {
        uint8_t   delegate_id;
        Signature signature;
    };

    // Aggregate sign
    //TODO ask with Devon: this function aggregates the signatures to one, then put it in message.
    //                       Didn't sign new data
    template<typename MSG>
    void Sign(MSG & message, const std::vector<DelegateSignature> & signatures) throw(bls::Exception)
    {
        // Set participation map:
    	PublicKeyVec keyvec;
    	SignatureVec sigvec;
        for(auto & sig : signatures)
        {
            message.participation_map[sig.delegate_id] = true;
            SignatureReal sigReal;
        	string sig_str(reinterpret_cast<const char*>(&message.signature), CONSENSUS_SIG_SIZE);
        	sigReal.deserialize(sig_str);
        	//TODO ask Devon: are the individual signatures already verified?
        	sigvec.push_back(sigReal);
            keyvec.push_back(_keys[sig.delegate_id]);
        }

        // Now sign the message with the aggregate sig
        SignatureReal asig;
        asig.aggregateFrom(sigvec, keyvec);

    	string asig_str;
    	asig.serialize(asig_str);
    	memcpy(&message.signature, asig_str.data(), CONSENSUS_SIG_SIZE);
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
    template<MessageType type, MessageType type2>
    bool Validate(const PostPhaseMessage<type> & message, const StandardPhaseMessage<type2> & reference)
    {
        // Use message.participation_map
        // to identify those delegates that
        // signed, and access their public
        // keys using _keys.
    	if (message.participation_map.none())
    		return false;

    	//public key agg
        auto apk = PublicKeyAggregate(message.participation_map);

        //message
    	string msg(reinterpret_cast<const char*>(&message), PostPhaseMessage<type>::HASHABLE_BYTES);

    	//sig
    	SignatureReal sig;
    	string sig_str(reinterpret_cast<const char*>(&message.signature), CONSENSUS_SIG_SIZE);
    	try{
    		sig.deserialize(sig_str);
    	}
    	catch (const bls::Exception &)
    	{
    		return false;
    	}
        return sig.verify(apk, msg);
    }

    // Single validation
    //TODO probably need another version that verifies other node's signature
    template<MessageType type>
    bool Validate(const StandardPhaseMessage<type> & message, uint8_t delegate_id)
    {
    	SignatureReal sig;
    	string msg(reinterpret_cast<const char*>(&message), StandardPhaseMessage<type>::HASHABLE_BYTES);
    	string sig_str(reinterpret_cast<const char*>(&message.signature), CONSENSUS_SIG_SIZE);

    	try{
    		sig.deserialize(sig_str);
    	}
    	catch (const bls::Exception &)
    	{
    		return false;
    	}
    	return sig.verify(_keypair.pub, msg);
    }

    void OnPublicKey(uint8_t delegate_id, const PublicKey & key) throw(bls::Exception);

    PublicKey GetPublicKey();

private:

    Log     _log;
    Keys    _keys;
    KeyPair _keypair;
};


