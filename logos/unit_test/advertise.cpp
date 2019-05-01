#include <iostream>
#include <gtest/gtest.h>

#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/ecies.hpp>


TEST(Advertise, serialize)
{
    std::cout << "--- Started Advertise test\n";
    bls::KeyPair bls;
    ECIESKeyPair ecies("3041020100301306072a8648ce3d020106082a8648ce3d03010704273025"
                       "0201010420ccc3cdefdef6fe4c5ce4c2282b0d89d097c58ea5de5bd43aec"
                       "5f6a2691d4a8d7 3059301306072a8648ce3d020106082a8648ce3d03010"
                       "7034200048e1ad798008baac3663c0c1a6ce04c7cb632eb504562de92384"
                       "5fccf39d1c46dee52df70f6cf46f1351ce7ac8e92055e5f168f5aff24bca"
                       "ab7513d447fd677d3", true);
    uint32_t epoch_number = 3;
    uint8_t delegate_id = 4;
    uint8_t encr_delegate_id = 5;
    const char *ip = "172.11.45.32";
    uint16_t port = 50601;
    uint16_t json_port = 51600;
    bool add = true;

    {
        AddressAd ad(epoch_number, delegate_id, encr_delegate_id, ip, port);
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