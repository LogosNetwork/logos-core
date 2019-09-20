// @file
// ECIES related declarations
//
#pragma once
#include <logos/request/fields.hpp>
#include <logos/lib/utility.hpp>
#include <logos/consensus/messages/byte_arrays.hpp>

#include <cryptopp/eccrypto.h>
#include <cryptopp/pubkey.h>
#include <cryptopp/ecp.h>
#include <cryptopp/asn.h>
#include <cryptopp/oids.h>
#include <blake2/blake2.h>

#include <boost/property_tree/ptree.hpp>

#include <sstream>
#include <iomanip>

/*#include <cryptopp/cryptlib.h>

#include <cryptopp/osrng.h>
#include <cryptopp/integer.h>*/

using CryptoPP::ECIES;
using CryptoPP::ECP;
using CryptoPP::StringSink;
using CryptoPP::ArraySink;
using CryptoPP::StringSource;
using CryptoPP::PK_EncryptorFilter;
using CryptoPP::PK_DecryptorFilter;
using RawKey = ByteArray<32>;

template<typename Key>
class ECIESKey : public Key {
public:
    ECIESKey() = default;
    virtual ~ECIESKey() = default;

    void FromString(std::string &str, bool is_hex)
    {
        if (is_hex)
        {
            FromHexString(str);
        } else {
            FromString(str);
        }
    }

    virtual std::string ToString() const = 0;
    virtual void FromString(const std::string &in) = 0;

    size_t Serialize(logos::stream &stream, bool is_hex = false) const
    {
        return logos::write(stream, is_hex ? (ToHexString()):(ToString()));
    }

    void SerializeJson(boost::property_tree::ptree & tree, bool is_hex = true) const
    {
        tree.put(request::fields::ECIES_KEY,is_hex?(ToHexString()):ToString());
    }

    std::string ToHexString() const
    {
        return logos::unicode_to_hex(ToString());
    }

    bool Deserialize(logos::stream &stream, bool is_hex = false)
    {
        std::string text;
        if (logos::read(stream, text))
        {
            return true;
        }
        FromString(text, is_hex);
        return false;
    }

    bool DeserializeJson(const boost::property_tree::ptree & tree, bool is_hex = true)
    {
        std::string ecies_key = tree.get<std::string>(request::fields::ECIES_KEY);
        FromString(ecies_key, is_hex);
        return true;
    }

    void FromHexString(const std::string &in)
    {
        FromString(logos::hex_to_unicode(in));
    }

    void Hash(blake2b_state & hash) const
    {
        std::string str = ToString();
        blake2b_update(&hash, str.data(), str.length());
    }
};

class ECIESPrivateKey : public ECIESKey<CryptoPP::DL_PrivateKey_EC<ECP>> {
    using Key = CryptoPP::DL_PrivateKey_EC<ECP>;

public:
    ECIESPrivateKey() : ECIESKey<CryptoPP::DL_PrivateKey_EC<ECP>> () {
        CryptoPP::AutoSeededRandomPool prng;
        Initialize(prng, CryptoPP::ASN1::secp256r1());
    }
    ECIESPrivateKey(std::string &str, bool is_hex) : ECIESKey<CryptoPP::DL_PrivateKey_EC<ECP>> ()
    {
        ECIESKey<CryptoPP::DL_PrivateKey_EC<ECP>>::FromString(str, is_hex);
    }
    ECIESPrivateKey(RawKey const & raw) : ECIESKey<CryptoPP::DL_PrivateKey_EC<ECP>> () {
        CryptoPP::Integer x;
        x.Decode(raw.data(), raw.size());
        Initialize(CryptoPP::ASN1::secp256r1(), x);
    };
    ~ECIESPrivateKey() = default;

    std::string ToString() const override
    {
        std::string out_str;
        StringSink out_sink(out_str);
        GetPrivateExponent().Encode(out_sink, CONSENSUS_PRIV_KEY_SIZE);
        return out_str;
    }

    void FromString(const std::string &in) override
    {
        StringSource src(in, true/*pumpAll*/);
        CryptoPP::Integer x;
        x.Decode(src, CONSENSUS_PRIV_KEY_SIZE);
        Initialize(CryptoPP::ASN1::secp256r1(), x);
    }

    template<typename ... Args>
    void Decrypt(const std::string &cyphertex, Args ... args)
    {
        CryptoPP::AutoSeededRandomPool prng;
        ECIES<ECP>::Decryptor d(*this);
        StringSource stringSource(cyphertex, true, new PK_DecryptorFilter(prng, d, new ArraySink(args ... )));
    }
    void Decrypt(const std::string &cyphertex, std::string &text)
    {
        CryptoPP::AutoSeededRandomPool prng;
        ECIES<ECP>::Decryptor d(*this);
        StringSource stringSource(cyphertex, true, new PK_DecryptorFilter(prng, d, new StringSink(text)));
    }

    bool operator==(const ECIESPrivateKey &rhs) const
    {
        return this->GetPrivateExponent() == rhs.GetPrivateExponent();
    }
};

class ECIESPublicKey : public ECIESKey<CryptoPP::DL_PublicKey_EC<ECP>> {
public:
    ECIESPublicKey() : ECIESKey<CryptoPP::DL_PublicKey_EC<ECP>> () {}
    ECIESPublicKey(const ECIESPrivateKey &pkey) : ECIESKey<CryptoPP::DL_PublicKey_EC<ECP>> ()
    {
        AssignFrom(pkey);
    }
    ECIESPublicKey(std::string &str, bool is_hex = true) : ECIESKey<CryptoPP::DL_PublicKey_EC<ECP>> ()
    {
        ECIESKey<CryptoPP::DL_PublicKey_EC<ECP>>::FromString(str, is_hex);
    }
    ~ECIESPublicKey() = default;

    std::string ToString() const override
    {
        std::string str;
        auto P (GetPublicElement());
        StringSink sink(str);
        P.x.Encode(sink, CONSENSUS_PRIV_KEY_SIZE);
        P.y.Encode(sink, CONSENSUS_PRIV_KEY_SIZE);
        return str;
    }

    void FromString(const std::string &in) override
    {
        StringSource src(in, true/*pumpAll*/);
        typename ECP::Point P;
        P.identity = false;
        P.x.Decode(src, CONSENSUS_PRIV_KEY_SIZE);
        P.y.Decode(src, CONSENSUS_PRIV_KEY_SIZE);
        Initialize(CryptoPP::ASN1::secp256r1(), P);
    }

    template <typename ... Args>
    void Encrypt(std::string &cyphertext, Args ... args)
    {
        CryptoPP::AutoSeededRandomPool prng;
        ECIES<ECP>::Encryptor e(*this);
        StringSource stringSource(args ..., true, new PK_EncryptorFilter(prng, e, new StringSink(cyphertext)));
    }

    bool operator==(const ECIESPublicKey &rhs) const
    {
        return this->GetGroupParameters() == rhs.GetGroupParameters() && this->GetPublicElement() == rhs.GetPublicElement();
    }
};

struct ECIESKeyPair {
    ECIESKeyPair() : prv()
    {
        pub.AssignFrom(prv);
    }
    ~ECIESKeyPair() = default;
    ECIESKeyPair(RawKey const & raw) : prv(raw)
    {
        pub.AssignFrom(prv);
    }

    bool operator==(const ECIESKeyPair &rhs) const
    {
        return this->prv == rhs.prv && this->pub == rhs.pub;
    }

    ECIESPrivateKey prv;
    ECIESPublicKey pub;
};
