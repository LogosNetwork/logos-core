#include <gtest/gtest.h>
#include <logos/identity_management/sleeve.hpp>

using byte = uint8_t;

class Sleeve;

TEST (Sleeve, KeyEncoding)
{
    Log log;
    LOG_DEBUG(log) << "BLS Encoding tests.";

    bls::KeyPair bkp1 (ByteArray<PL>("37c1cd7cffb71c8acaba3c05360934bb89c742392b9635bd1a6379fe739f3e01"));

    LOG_DEBUG(log) << "BKP1: " << bkp1.pub.to_string();

    bls::KeyPair bls;

    std::string bls_prv_str;
    bls.prv.getStr(bls_prv_str, mcl::IoMode::IoSerialize);
    bls::SecretKey bls_prv;
    bls_prv.setStr(bls_prv_str, mcl::IoMode::IoSerialize);
    ASSERT_EQ(bls_prv, bls.prv);

    std::string bls_pub_str;
    bls.pub.getStr(bls_pub_str, mcl::IoMode::IoSerialize);
    bls::PublicKey bls_pub;
    bls_pub.setStr(bls_pub_str, mcl::IoMode::IoSerialize);
    ASSERT_EQ(bls_pub, bls.pub);

    bls::PublicKey bls_pub_from_prv;
    bls_prv.getPublicKey(bls_pub_from_prv);
    ASSERT_EQ(bls_pub_from_prv, bls.pub);

    LOG_DEBUG(log) << "ECIES Encoding tests.";
    ECIESKeyPair ecies;

    std::string ecies_pub_str = ecies.pub.ToString();
    ECIESPublicKey ecies_new_pub (ecies_pub_str, false);
    ASSERT_EQ(ecies_new_pub, ecies.pub);

    std::string ecies_pub_str_hex = ecies.pub.ToHexString();
    ECIESPublicKey ecies_new_pub_hex (ecies_pub_str_hex, true);
    ASSERT_EQ(ecies_new_pub_hex, ecies.pub);

    std::string ecies_prv_str = ecies.prv.ToString();
    ECIESPrivateKey ecies_new_prv (ecies_prv_str, false);
    ASSERT_EQ(ecies_new_prv, ecies.prv);

    std::string ecies_prv_str_hex = ecies.prv.ToHexString();
    ECIESPrivateKey ecies_new_prv_hex (ecies_prv_str_hex, true);
    ASSERT_EQ(ecies_new_prv_hex, ecies.prv);
}

TEST (Sleeve, KeyEncryption)
{
    Log log;

    boost::filesystem::path db_file("./test_db/unit_test_sleeve_db.lmdb");
    bool err;

    {
        // cleanup
        Sleeve sleeve(db_file, 1024, err);
        ASSERT_FALSE(err);
        logos::transaction tx(sleeve._env, nullptr, true);
        ASSERT_FALSE(mdb_drop(tx, sleeve._sleeve_handle, 0));

        ////////////////////////////////////////
        /// Test encryption and decryption, putting and getting authenticated encrypted entries
        CryptoPP::AutoSeededRandomPool prng;

        PlainText pdata, rpdata;
        CipherText cipher, db_cipher;
        Sleeve::IV iv, db_iv;
        Sleeve::EncryptionKey key;

        prng.GenerateBlock(pdata.data(), pdata.size());
        prng.GenerateBlock(key.data(), key.size());
        prng.GenerateBlock(iv.data(), iv.size());

        Sleeve::AuthenticatedEncrypt(pdata, cipher, key, iv);
        ASSERT_TRUE(Sleeve::AuthenticatedDecrypt(cipher, rpdata, key, iv));
        ASSERT_EQ(pdata, rpdata);

        sleeve.AEEntryPut(Sleeve::_bls_locator, cipher, iv, tx);
        ASSERT_TRUE(sleeve.AEEntryGet(Sleeve::_bls_locator, db_cipher, db_iv, tx));
        ASSERT_EQ(cipher, db_cipher);
        ASSERT_EQ(iv, db_iv);

        rpdata.clear();
        ASSERT_TRUE(Sleeve::AuthenticatedDecrypt(db_cipher, rpdata, key, db_iv));
        ASSERT_EQ(pdata, rpdata);

        ////////////////////////////////////////
        /// Test locking / unlocking
        ASSERT_FALSE(mdb_drop(tx, sleeve._sleeve_handle, 0));
        sleeve.Initialize(tx);

        // newly initialized Sleeve is unlocked
        ASSERT_TRUE(sleeve.IsUnlocked());
        ASSERT_TRUE(sleeve.Lock());
        // cannot lock again
        ASSERT_FALSE(sleeve.Lock());
        ASSERT_FALSE(sleeve.IsUnlocked());
    }

    Sleeve sleeve(db_file, 1024, err);
    ASSERT_FALSE(err);
    logos::transaction tx(sleeve._env, nullptr, true);

    // _password is reset on reload
    ASSERT_FALSE(sleeve.IsUnlocked());
    ASSERT_FALSE(sleeve.Lock());

    // unlock with invalid password
    ASSERT_FALSE(sleeve.Unlock(" ", tx));
    // unlock with valid password
    ASSERT_TRUE(sleeve.Unlock("", tx));
    // cannot unlock twice
    ASSERT_FALSE(sleeve.Unlock("", tx));
    ASSERT_TRUE(sleeve.IsUnlocked());

    ////////////////////////////////////////
    /// Test password changes
    // change password
    std::string new_password("new password");
    ASSERT_TRUE(sleeve.Rekey(new_password, tx));

    ASSERT_TRUE(sleeve.Lock());
    ASSERT_FALSE(sleeve.Unsleeve(tx));
    ASSERT_FALSE(sleeve.Unlock("", tx));
    ASSERT_TRUE(sleeve.Unlock(new_password, tx));

    ////////////////////////////////////////
    /// Test key storage

    ECIESKeyPair ecies;
    PlainText ecies_prv;
    ecies.prv.GetPrivateExponent().Encode(ecies_prv.data(), ecies_prv.size());

    bls::KeyPair bls;
    PlainText bls_prv(bls.prv.to_string());

    ASSERT_TRUE(sleeve.StoreKeys(bls_prv, ecies_prv, false, tx));

    // Test override
    ASSERT_FALSE(sleeve.StoreKeys(bls_prv, ecies_prv, false, tx));
    ASSERT_TRUE(sleeve.StoreKeys(bls_prv, ecies_prv, true, tx));

    ASSERT_TRUE(sleeve.Unsleeve(tx));
    ASSERT_TRUE(sleeve.StoreKeys(bls_prv, ecies_prv, false, tx));

    auto ecies_db (sleeve.GetECIESKey(tx));
    ASSERT_NE(ecies_db, nullptr);
    ASSERT_EQ(ecies_db->prv, ecies.prv);
    ASSERT_EQ(ecies_db->pub, ecies.pub);

    auto bls_db (sleeve.GetBLSKey(tx));
    ASSERT_NE(bls_db, nullptr);

    ASSERT_EQ(bls_db->prv, bls.prv);
    ASSERT_EQ(bls_db->pub, bls.pub);

    ASSERT_TRUE(sleeve.IsSleeved(tx));
}
