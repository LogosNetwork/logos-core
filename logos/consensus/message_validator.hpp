#pragma once

#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/messages/common.hpp>
#include <logos/lib/log.hpp>
#include <logos/common.hpp>

#include <bls/bls.hpp>

class MessageValidator
{
    using KeyPair       = bls::KeyPair;
    using PublicKeyReal = bls::PublicKey;
    using PublicKeyVec  = bls::PublicKeyVec;
    using SignatureReal = bls::Signature;
    using SignatureVec  = bls::SignatureVec;

public:

    struct DelegateSignature
    {
        uint8_t   delegate_id;
        DelegateSig signature;
    };

    MessageValidator(DelegateKeyStore & key_store, KeyPair &key_pair);

    //single
    void Sign(const BlockHash & hash, DelegateSig & sig)
    {
        string hash_str(reinterpret_cast<const char*>(hash.data()), HASH_SIZE);

        SignatureReal sig_real;
        _keypair.prv.sign(sig_real, hash_str);

        string sig_str;
        sig_real.serialize(sig_str);
        memcpy(&sig, sig_str.data(), CONSENSUS_SIG_SIZE);
    }
    //single
    bool Validate(const BlockHash & hash, const DelegateSig & sig, uint8_t delegate_id)
    {
        //hash
        string hash_str(reinterpret_cast<const char*>(hash.data()), HASH_SIZE);

        //deserialize sig
        SignatureReal sig_real;
        string sig_str(reinterpret_cast<const char*>(&sig), CONSENSUS_SIG_SIZE);

        try
        {
            sig_real.deserialize(sig_str);
        }
        catch (const bls::Exception &)
        {
            LOG_ERROR(_log) << "MessageValidator - Failed to deserialize signature.";
            return false;
        }

        //verify
        return sig_real.verify(_keys.GetPublicKey(delegate_id), hash_str);
    }
    //aggregate
    bool AggregateSignature(const std::vector<DelegateSignature> & signatures, AggSignature & agg_sig)
    {
        PublicKeyVec keyvec;
        SignatureVec sigvec;

        std::set<uint8_t> participants;

        for(auto & sig : signatures)
        {
            auto did = sig.delegate_id;
            if(participants.find(did) != participants.end())
            {
                LOG_WARN(_log) << "MessageValidator - duplicate single sig from "<<(uint)did;
                continue;
            }
            participants.insert(did);
            agg_sig.map[did] = true;

            SignatureReal sigReal;
            string sig_str(reinterpret_cast<const char*>(&sig.signature), CONSENSUS_SIG_SIZE);
            try
            {
                sigReal.deserialize(sig_str);
            }
            catch(bls::Exception &e)
            {
                LOG_ERROR (_log) << "MessageValidator - Aggregate sign, failed to deserialize a signature";
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
        memcpy(&agg_sig.sig, asig_str.data(), CONSENSUS_SIG_SIZE);
        return true;
    }

    //aggregate
    bool Validate(const BlockHash & hash, const AggSignature & sig)
    {
        //public key agg
        auto apk = _keys.GetAggregatedPublicKey(sig.map);

        //hash
        string hash_str(reinterpret_cast<const char*>(hash.data()), HASH_SIZE);

        //deserialize sig
        SignatureReal sig_real;
        string sig_str(reinterpret_cast<const char*>(&sig.sig), CONSENSUS_SIG_SIZE);

        try
        {
            sig_real.deserialize(sig_str);
        }
        catch (const bls::Exception &)
        {
            LOG_ERROR (_log) << "MessageValidator - Aggregate validate, failed to deserialize the signature";
            return false;
        }

        //verify
        return sig_real.verify(apk, hash_str);
    }

    DelegatePubKey GetPublicKey();

private:

    Log                 _log;
    KeyPair             _keypair;
    DelegateKeyStore &  _keys;
};
