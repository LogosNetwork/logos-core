#include <iostream>
#include <gtest/gtest.h>

#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/ecies.hpp>

std::string string_to_hex(std::string& in) {
    std::stringstream ss;

    ss << std::hex << std::setfill('0');
    for (size_t i = 0; in.length() > i; ++i) {
        ss << std::setw(2) << static_cast<unsigned int>(static_cast<unsigned char>(in[i]));
    }

    return ss.str();
}

TEST(Advertise, ecies)
{
    std::cout << "--- Started ecies test\n";
    ECIESKeyPair pair;
    std::string text = "The Logos Network is a distributed, trustless transaction network designed for extreme scalability";
    std::string cyphertext = "";

    pair.pub.Encrypt(cyphertext, text);

    std::string text1 = "";
    std::string text2 = "";
    std::vector<uint8_t> buf(text.size());

    pair.prv.Decrypt(cyphertext, text1);
    pair.prv.Decrypt(cyphertext, buf.data(), buf.size());

    std::for_each(buf.begin(), buf.end(), [&](auto item){text2+=item;});
    std::cout << "plain text\n";
    std::cout << string_to_hex(text) << "\n";
    std::cout << "cyphertext\n";
    std::cout << string_to_hex(cyphertext) << "\n";

    ASSERT_EQ(text, text1);
    ASSERT_EQ(text, text2);
}

TEST(Advertise, serialize)
{
    std::cout << "--- Started Advertise test\n";
    bls::KeyPair bls;
    ECIESKeyPair ecies;
    uint32_t epoch_number = 3;
    uint8_t delegate_id = 4;
    uint8_t encr_delegate_id = 5;
    const char *ip = "172.11.45.32";
    uint16_t port = 50601;
    uint16_t json_port = 51600;
    bool add = true;

    {
        AddressAd ad(epoch_number, delegate_id, encr_delegate_id, ip, port);
        ad.consensus_version = 123;
        MessageValidator::Sign(ad.Hash(), ad.signature, [&bls](bls::Signature &sig_real, const std::string &hash_str) {
            bls.prv.sign(sig_real, hash_str);
        });
        std::vector<uint8_t> buf;
        ad.Serialize(buf, ecies.pub);
        std::cout << "Serialized AddressAd\n";

        logos::bufferstream str(buf.data(), buf.size());
        bool error = false;
        try {
            AddressAd ad1(error, str, [&ecies](const std::string &cyphertext, uint8_t *buf, size_t size) {
                ecies.prv.Decrypt(cyphertext, buf, size);
            });
            std::cout << "Deserialized AddressAd " << error << "\n";

            ASSERT_FALSE(error);

            auto ret = MessageValidator::Validate(ad1.Hash(), ad1.signature, bls.pub);
            std::cout << "Validated AddressAd " << ret << "\n";

            ASSERT_EQ(ad1.consensus_version, 123);
            ASSERT_TRUE(ad == ad1);
        }
        catch (const std::exception &ex) {
            std::cout << "AddressAd decryption failed\n";
            ASSERT_FALSE(true);
        }
    }

    {
        AddressAdTxAcceptor adtxa(epoch_number, delegate_id, ip, port, json_port, add);
        MessageValidator::Sign(adtxa.Hash(), adtxa.signature, [&bls](bls::Signature &sig_real, const std::string &hash_str) {
            bls.prv.sign(sig_real, hash_str);
        });
        std::vector<uint8_t> buf;
        adtxa.Serialize(buf);

        std::cout << "Serialized AddressAdTxAcceptor\n";

        bool error = false;
        logos::bufferstream str(buf.data(), buf.size());
        AddressAdTxAcceptor adtxa1(error, str);
        std::cout << "Deserialized AddressAdTxAcceptor " << error << "\n";

        ASSERT_FALSE(error);

        auto ret = MessageValidator::Validate(adtxa1.Hash(), adtxa1.signature, bls.pub);
        std::cout << "Validated AddressAdTxAcceptor " << ret << "\n";
        ASSERT_TRUE(ret);

        ASSERT_TRUE(adtxa == adtxa1);
    }
}