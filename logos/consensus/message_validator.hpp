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

    MessageValidator(DelegateKeyStore & key_store);

    //single
    virtual void Sign(const BlockHash & hash, DelegateSig & sig);

    bool Validate(const BlockHash & hash, const DelegateSig & sig, uint8_t delegate_id)
    {
        auto pub_key = _keys.GetPublicKey(delegate_id);
        return Validate(hash, sig, pub_key);
    }

    //single
    static bool Validate(const BlockHash & hash, const DelegateSig & sig, const PublicKeyReal &pub_key)
    {
        Log log;
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
            LOG_ERROR(log) << "MessageValidator - Failed to deserialize signature.";
            return false;
        }

        //verify
        return sig_real.verify(pub_key, hash_str);
    }
    //aggregate
    bool AggregateSignature(const std::unordered_map<uint8_t,DelegateSignature> & signatures, AggSignature & agg_sig)
    {
        PublicKeyVec keyvec;
        SignatureVec sigvec;

        std::set<uint8_t> participants;

        for(auto & iter : signatures)
        {
            auto sig = iter.second;
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

    virtual DelegatePubKey GetPublicKey();

    static DelegatePubKey BlsPublicKey(bls::PublicKey &bls_pub)
    {
        std::string keystring;
        bls_pub.serialize(keystring);

        DelegatePubKey pk;
        memcpy(pk.data(), keystring.data(), CONSENSUS_PUB_KEY_SIZE);

        return pk;
    }

    template<typename F>
    static void Sign(const BlockHash &hash, DelegateSig & sig, F &&signee)
    {
        string hash_str(reinterpret_cast<const char*>(hash.data()), HASH_SIZE);

        bls::Signature sig_real;

        signee(sig_real, hash_str);

        string sig_str;
        sig_real.serialize(sig_str);
        memcpy(&sig, sig_str.data(), CONSENSUS_SIG_SIZE);
    }

private:

    Log                 _log;
    DelegateKeyStore &  _keys;
};
