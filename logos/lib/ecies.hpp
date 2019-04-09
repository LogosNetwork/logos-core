// @file
// ECIES related declarations
//
#pragma once
#include <logos/request/fields.hpp>
#include <logos/lib/utility.hpp>

#include <cryptopp/eccrypto.h>
#include <cryptopp/pubkey.h>
#include <cryptopp/ecp.h>
#include <blake2/blake2.h>

#include <boost/property_tree/ptree.hpp>

#include <sstream>
#include <iomanip>

/*#include <cryptopp/cryptlib.h>
#include <cryptopp/asn.h>
#include <cryptopp/oids.h>

#include <cryptopp/osrng.h>
#include <cryptopp/integer.h>*/

using CryptoPP::ECIES;
using CryptoPP::ECP;
using CryptoPP::StringSink;
using CryptoPP::ArraySink;
using CryptoPP::StringSource;
using CryptoPP::PK_EncryptorFilter;
using CryptoPP::PK_DecryptorFilter;

template<typename Key>
class ECIESKey : public Key {
public:
    ECIESKey() = default;
    ECIESKey(std::string &str, bool is_hex)
    {
        FromString(str, is_hex);
    }
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

    std::string ToString() const
    {
        std::string str;
        StringSink sink(str);
        Key::Save(sink);
        return str;
    }

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
        std::string in = ToString();
        std::stringstream str;

        str << std::hex << std::setfill('0');
        for (size_t i = 0; in.length() > i; ++i) {
            str << std::setw(2) << static_cast<unsigned int>(static_cast<unsigned char>(in[i]));
        }
        return str.str();
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

    void FromString(const std::string &in)
    {
        StringSource src(in, true);
        Key::Load(src);
    }

    void FromHexString(const std::string &in)
    {
        std::string output;

        assert((in.length() % 2) == 0);

        size_t cnt = in.length() / 2;

        for (size_t i = 0; cnt > i; ++i) {
            uint32_t s = 0;
            std::stringstream ss;
            ss << std::hex << in.substr(i * 2, 2);
            ss >> s;

            output.push_back(static_cast<unsigned char>(s));
        }
        FromString(output);
    }

    void Hash(blake2b_state & hash) const
    {
        std::string str = ToString();
        blake2b_update(&hash, str.data(), str.length());
    }
};

class ECIESPublicKey : public ECIESKey<CryptoPP::DL_PublicKey_EC<ECP>> {
public:
    ECIESPublicKey() : ECIESKey<CryptoPP::DL_PublicKey_EC<ECP>> () {}
    ECIESPublicKey(std::string &str, bool is_hex) : ECIESKey<CryptoPP::DL_PublicKey_EC<ECP>> (str, is_hex){}
    ~ECIESPublicKey() = default;

    template <typename ... Args>
    void Encrypt(std::string &cyphertext, Args ... args)
    {
        CryptoPP::AutoSeededRandomPool prng;
        ECIES<ECP>::Encryptor e(*this);
        StringSource stringSource(args ..., true, new PK_EncryptorFilter(prng, e, new StringSink(cyphertext)));
    }
};

class ECIESPrivateKey : public ECIESKey<CryptoPP::DL_PrivateKey_EC<ECP>> {
public:
    ECIESPrivateKey() : ECIESKey<CryptoPP::DL_PrivateKey_EC<ECP>> () {}
    ECIESPrivateKey(std::string &str, bool is_hex) : ECIESKey<CryptoPP::DL_PrivateKey_EC<ECP>> (str, is_hex){}
    ~ECIESPrivateKey() = default;

    template<typename ... Args>
    void Decrypt(const std::string &cyphertex, Args ... args)
    {
        CryptoPP::AutoSeededRandomPool prng;
        ECIES<ECP>::Decryptor d(*this);
        StringSource stringSource(cyphertex, true, new PK_DecryptorFilter(prng, d, new ArraySink(args ... )));
    }
};

struct ECIESKeyPair {
    ECIESKeyPair() = default;
    ~ECIESKeyPair() = default;
    ECIESKeyPair(std::string &&hex, bool is_hex = true)
    {
        std::stringstream str(hex);
        std::string prv_hex, pub_hex;
        str >> prv_hex >> pub_hex;
        if (is_hex) {
            prv.FromHexString(prv_hex);
            pub.FromHexString(pub_hex);
        }
        else {
            prv.FromString(prv_hex);
            pub.FromString(pub_hex);
        }
    }
    ECIESPublicKey pub;
    ECIESPrivateKey prv;
};
