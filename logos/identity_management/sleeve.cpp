#include <logos/identity_management/sleeve.hpp>
#include <logos/lib/numbers.hpp>
#include <logos/lib/trace.hpp>

#include <argon2.h>
#include <cryptopp/filters.h>
#include <cryptopp/gcm.h>

std::string SleeveResultToString(sleeve_code code)
{
    std::string ret;

    switch(code)
    {
        case sleeve_code::success:
            ret = "Success";
            break;
        case sleeve_code::identity_control_disabled:
            ret = "Identity control is disabled";
            break;
        case sleeve_code::invalid_password:
            ret = "Password is incorrect";
            break;
        case sleeve_code::sleeve_locked:
            ret = "Sleeve is locked";
            break;
        case sleeve_code::sleeve_already_unlocked:
            ret = "Sleeve cannot be unlocked twice";
            break;
        case sleeve_code::already_sleeved:
            ret = "Already sleeved but \"overwrite\" is not set to true";
            break;
        case sleeve_code::setting_already_applied:
            ret = "Received command to activate / deactivate when already at desired setting";
            break;
        case sleeve_code::already_scheduled:
            ret = "Already have an activation setting change scheduled";
            break;
        case sleeve_code::nothing_scheduled:
            ret = "No future activation or deactivation scheduled";
            break;
        case sleeve_code::invalid_setting_epoch:
            ret = "Setting scheduled for an old epoch";
            break;
        case sleeve_code::epoch_transition_started:
            ret = "Received scheduling command after the transition events for the specified epoch has already started";
            break;
    }
    return ret;
}

template<size_t L>
Fan<L>::Fan(ByteArray<L> const & key, size_t count)
{
    std::unique_ptr<ByteArray<L>> first(new ByteArray<L>(key));
    for (auto i(1); i < count; i++)
    {
        std::unique_ptr<ByteArray<L>> entry(new ByteArray<L>);
        logos::random_pool.GenerateBlock(entry->data(), entry->size());
        *first ^= *entry;
        values.push_back(std::move(entry));
    }
    values.push_back(std::move(first));
}

template<size_t L>
void Fan<L>::CopyValueTo(ByteArray<L> & prv)
{
    std::lock_guard<std::mutex> lock(mutex);
    RetrieveValue(prv);
}

template<size_t L>
void Fan<L>::SetValueFrom(ByteArray<L> const & prv)
{
    std::lock_guard<std::mutex> lock(mutex);
    // retrieve old value
    ByteArray<L> old_prv;
    RetrieveValue(old_prv);
    // XOR again so that the old value cancels out
    *(values[0]) ^= old_prv;
    // XOR to store the new value
    *(values[0]) ^= prv;
}

template<size_t L>
void Fan<L>::RetrieveValue(ByteArray<L> & prv)
{
    assert(!mutex.try_lock());
    prv.clear();
    for (auto const & i : values)
    {
        prv ^= *i;
    }
}

template<size_t l>
void Fan<l>::Clear()
{
    SetValueFrom(ByteArray<l>{});
}

void KDF::PHS(ByteArray<32> & result, std::string const & password, Byte32Array const & salt)
{
    std::lock_guard<std::mutex> lock (mutex);
    auto success (argon2_hash (1, Sleeve::kdf_work, 1,
            password.data(), password.size(),
            salt.bytes.data(), salt.bytes.size(),
            result.data(), result.size(),
            nullptr, 0, Argon2_d, 0x10));
    assert(success == 0);
    (void)success;
}

Byte32Array const Sleeve::_version_locator(0);
Byte32Array const Sleeve::_sleeve_key_locator(1);
Byte32Array const Sleeve::_salt_locator(2);
Byte32Array const Sleeve::_bls_locator(3);
Byte32Array const Sleeve::_ecies_locator(4);

Sleeve::Sleeve(boost::filesystem::path const & path, unsigned fanout_size, bool & error)
    : _password(0, fanout_size)
    , _sleeve_key_cipher(0, fanout_size)
    , _env(error, path, 1 /*only need 1 database in this environment for now*/)
{
    if (error)
    {
        LOG_FATAL(_log) << "Sleeve::Sleeve - Cannot open Sleeve LMDB environment";
        trace_and_halt();
    }
    logos::transaction tx(_env, nullptr, true);
    if (!OpenDB(tx))
    {
        LOG_FATAL(_log) << "Sleeve::Sleeve - Cannot open Sleeve database";
        trace_and_halt();
    }
    Initialize(tx);
}

void Sleeve::Initialize(logos::transaction const & tx)
{
    if (HasContent(tx))
    {
        LOG_DEBUG(_log) << "Sleeve::Initialize - loading sleeve key ciphertext and IV existing database.";
        CipherText cipher;
        AEEntryGet(_sleeve_key_locator, cipher, _sleeve_key_iv, tx);
        _sleeve_key_cipher.SetValueFrom(cipher);
        // Note that the Sleeve is Locked even if the password isn't
        // changed from the initial "" value, since _password is not set
        return;
    }

    VersionPut(tx, _version_current);

    // generate and store salt
    Byte32Array salt;
    logos::random_pool.GenerateBlock(salt.bytes.data(), salt.bytes.size());
    EntryPutRaw(_salt_locator, logos::mdb_val(salt), tx);

    // generate and encrypt (with empty password) sleeve master key
    EncryptionKey sleeve_key;
    logos::random_pool.GenerateBlock(sleeve_key.data(), sleeve_key.size());

    EncryptionKey derived_key;
    DeriveKey(derived_key, "", tx);  // initial password is empty
    _password.SetValueFrom(derived_key);

    logos::random_pool.GenerateBlock(_sleeve_key_iv.data(), _sleeve_key_iv.size());

    CipherText cipher;
    AuthenticatedEncrypt(sleeve_key, cipher, derived_key, _sleeve_key_iv);

    // store encrypted master key in db, along with iv
    AEEntryPut(_sleeve_key_locator, cipher, _sleeve_key_iv, tx);

    // store encrypted master key in memory
    CipherText sleeve_key_enc;
    sleeve_key_enc = cipher;
    _sleeve_key_cipher.SetValueFrom(sleeve_key_enc);
}

sleeve_status Sleeve::Rekey(std::string const & password, logos::transaction const & tx)
{
    std::lock_guard<std::mutex> lock(_mutex);

    EncryptionKey sleeve_key;
    // Check unlocked (retrieving sleeve key in plaintext)
    if (!IsUnlocked(sleeve_key))
    {
        LOG_ERROR(_log) << "Sleeve::Rekey - sleeve locked.";
        return sleeve_code::sleeve_locked;
    }

    // key-derive from new password & store in memory
    EncryptionKey derived_key;
    DeriveKey(derived_key, password, tx);
    _password.SetValueFrom(derived_key);

    // We can get away with not generating a new IV here, since a different
    // (key-derived) key is used to encrypt the sleeve master key

    // encrypt sleeve key with new key-derived key
    CipherText sleeve_key_cipher;
    AuthenticatedEncrypt(sleeve_key, sleeve_key_cipher, derived_key, _sleeve_key_iv);

    // store in database
    AEEntryPut(_sleeve_key_locator, sleeve_key_cipher, _sleeve_key_iv, tx);

    // store in memory
    _sleeve_key_cipher.SetValueFrom(sleeve_key_cipher);

    return sleeve_code::success;
}

bool Sleeve::IsUnlocked(EncryptionKey & plain)
{
    EncryptionKey password;
    _password.CopyValueTo(password);

    CipherText cipher;
    _sleeve_key_cipher.CopyValueTo(cipher);

    return AuthenticatedDecrypt(cipher, plain, password, _sleeve_key_iv);
}

void Sleeve::AuthenticatedEncrypt(PlainText const & plain, CipherText & cipher, EncryptionKey const & key, IV const & iv)
{
    try
    {
        CryptoPP::GCM<CryptoPP::AES>::Encryption e;
        e.SetKeyWithIV(key.data(), sizeof(key), iv.data(), sizeof(iv));
        CryptoPP::ArraySource as(plain.data(), sizeof(plain), true /*pumpAll*/,
                new CryptoPP::AuthenticatedEncryptionFilter(e,
                        new CryptoPP::ArraySink(cipher.data(), sizeof(cipher)), false, AES256GCM_TAG_SIZE));
    }
    catch(CryptoPP::Exception & e)
    {
        Log log;
        LOG_FATAL(log) << "Sleeve::AuthenticatedEncrypt - " << e.what();
        trace_and_halt();
    }
}

bool Sleeve::AuthenticatedDecrypt(CipherText const & cipher, PlainText & plain, EncryptionKey const & key, IV const & iv)
{
    try
    {
        CryptoPP::GCM<CryptoPP::AES>::Decryption d;
        d.SetKeyWithIV(key.data(), sizeof(key), iv.data(), sizeof(iv));

        CryptoPP::AuthenticatedDecryptionFilter df(d,
                new CryptoPP::ArraySink(plain.data(), sizeof(plain)),
                CryptoPP::AuthenticatedDecryptionFilter::DEFAULT_FLAGS);

        // The ArraySource dtor will be called immediately
        //  after construction below. This will cause the
        //  destruction of objects it owns. To stop the
        //  behavior so we can get the decoding result from
        //  the DecryptionFilter, we must use a redirector
        //  or manually Put(...) into the filter without
        //  using a ArraySource.
        CryptoPP::ArraySource as(cipher.data(), sizeof(cipher), true,
                new CryptoPP::Redirector(df));

        if (!df.GetLastResult())
        {
            Log log;
            LOG_ERROR(log) << "Sleeve::AuthenticatedDecrypt - data integrity compromised";
            return false;
        }
    }
    catch(CryptoPP::Exception & e)
    {
        Log log;
        LOG_ERROR(log) << "Sleeve::AuthenticatedDecrypt - " << e.what();
        return false;
    }
    return true;
}

void Sleeve::DeriveKey(EncryptionKey & new_key, std::string const & password, logos::transaction const & tx)
{
    // retrieve salt
    Byte32Array salt;
    if (!EntryGet(_salt_locator, salt, tx)) /*error retrieving salt*/
    {
        // generate new salt and store in db; this is okay since
        // DeriveKey is only called when the Sleeve is unlocked
        // and when we need to re-encrypt sleeve key.
        logos::random_pool.GenerateBlock(salt.bytes.data(), salt.bytes.size());
        EntryPutRaw(_salt_locator, logos::mdb_val(salt), tx);
    }

    // derive key with argon2 hash
    _kdf.PHS(new_key, password, salt);
}

sleeve_status Sleeve::Unlock(std::string const & password, logos::transaction const & tx)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (IsUnlocked())
    {
        LOG_ERROR(_log) << "Sleeve::Unlock - already unlocked";
        return sleeve_code::sleeve_already_unlocked;
    }

    EncryptionKey derived_key;
    DeriveKey(derived_key, password, tx);

    _password.SetValueFrom(derived_key);
    if (IsUnlocked())
    {
        return sleeve_code::success;
    }
    LOG_ERROR(_log) << "Sleeve::Unlock - incorrect password.";
    return sleeve_code::invalid_password;
}

sleeve_status Sleeve::Lock()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!IsUnlocked())
    {
        LOG_ERROR(_log) << "Sleeve::Lock - already locked.";
        return sleeve_code::sleeve_locked;
    }

    _password.Clear();
    LOG_DEBUG(_log) << "Sleeve::Lock - locked.";
    return sleeve_code::success;
}

sleeve_status Sleeve::StoreKeys(PlainText const & bls_prv, PlainText const & ecies_prv, bool overwrite, logos::transaction const & tx)
{
    std::lock_guard<std::mutex> lock(_mutex);
    EncryptionKey sleeve_key;
    if (!IsUnlocked(sleeve_key))
    {
        LOG_ERROR(_log) << "Sleeve::StoreKeys - Sleeve is locked.";
        return sleeve_code::sleeve_locked;
    }
    // check if Sleeved. If yes and "overwrite" is false, return error

    if (KeysExist(tx) && !overwrite)
    {
        LOG_ERROR(_log) << "Sleeve::StoreKeys - Found existing keys but \"overwrite\" is false.";
        return sleeve_code::already_sleeved;
    }

    // otherwise, update database entry

    // encrypt
    IV bls_iv, ecies_iv;
    logos::random_pool.GenerateBlock(bls_iv.data(), bls_iv.size());
    logos::random_pool.GenerateBlock(ecies_iv.data(), ecies_iv.size());

    CipherText bls_cipher, ecies_cipher;
    AuthenticatedEncrypt(bls_prv, bls_cipher, sleeve_key, bls_iv);
    AuthenticatedEncrypt(ecies_prv, ecies_cipher, sleeve_key, ecies_iv);

    // store
    AEEntryPut(_bls_locator, bls_cipher, bls_iv, tx);
    AEEntryPut(_ecies_locator, ecies_cipher, ecies_iv, tx);

    LOG_DEBUG(_log) << "Sleeve::StoreKeys - stored BLS and ECIES keys, overwrite " << overwrite;

    return sleeve_code::success;
}

sleeve_status Sleeve::Unsleeve(logos::transaction const & tx)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!IsUnlocked())
    {
        return sleeve_code::sleeve_locked;
    }

    auto status(mdb_del(tx.handle, _sleeve_handle, logos::mdb_val(_bls_locator), nullptr));
    assert(!status || status == MDB_NOTFOUND);
    status = (mdb_del(tx.handle, _sleeve_handle, logos::mdb_val(_ecies_locator), nullptr));
    assert(!status || status == MDB_NOTFOUND);
    return sleeve_code::success;
}

void Sleeve::Reset(logos::transaction const & tx)
{
    auto status (mdb_drop(tx, _sleeve_handle, 0));
    assert (status == 0);
    Initialize(tx);
}

bool Sleeve::OpenDB(logos::transaction const & tx)
{
    // NULL because we only need one database
    bool success (mdb_dbi_open(tx.handle, nullptr, MDB_CREATE, &_sleeve_handle) == 0);
    return success;
}

bool Sleeve::HasContent(logos::transaction const & tx)
{
    MDB_val version_val;
    return mdb_get(tx.handle, _sleeve_handle, logos::mdb_val(_version_locator), &version_val) != MDB_NOTFOUND;
}

bool Sleeve::KeysExist(logos::transaction const & tx)
{
    logos::mdb_val value;  // ignored
    auto bls_status(mdb_get(tx, _sleeve_handle, logos::mdb_val(_bls_locator), value));
    assert (bls_status == 0 || bls_status == MDB_NOTFOUND);
    auto ecies_status(mdb_get(tx, _sleeve_handle, logos::mdb_val(_ecies_locator), value));
    assert (ecies_status == 0 || ecies_status == MDB_NOTFOUND);

    // Note that there is a chance the database gets corrupted and only one out of the two keys is stored,
    // in which case we clear the existing key.
    if (bls_status && !ecies_status)
    {
        // BLS not found, wipe ECIES
        assert (!mdb_del(tx.handle, _sleeve_handle, logos::mdb_val(_ecies_locator), nullptr));
    }
    else if (!bls_status && ecies_status)
    {
        // ECIES not found, wipe BLS
        assert (!mdb_del(tx.handle, _sleeve_handle, logos::mdb_val(_bls_locator), nullptr));
    }

    return !bls_status && !ecies_status;  // only return true if both are found

}

std::unique_ptr<Sleeve::BLSKeyPair> Sleeve::GetBLSKey(logos::transaction const & tx)
{
    std::lock_guard<std::mutex> lock(_mutex);
    EncryptionKey sleeve_key;
    if (!IsUnlocked(sleeve_key))
    {
        LOG_ERROR(_log) << "Sleeve::GetBLSKey - Sleeve is locked.";
        return nullptr;
    }

    CipherText cipher;
    IV iv;
    if (!AEEntryGet(_bls_locator, cipher, iv, tx))
    {
        LOG_ERROR(_log) << "Sleeve::GetBLSKey - entry does not exist.";
        return nullptr;
    }

    PlainText bls_prv_raw;
    if (!AuthenticatedDecrypt(cipher, bls_prv_raw, sleeve_key, iv))
    {
        LOG_ERROR(_log) << "Sleeve::GetBLSKey - cannot decrypt BLS private key.";
        return nullptr;
    }

    return std::make_unique<Sleeve::BLSKeyPair>(bls_prv_raw);
}

std::unique_ptr<ECIESKeyPair> Sleeve::GetECIESKey(logos::transaction const & tx)
{
    std::lock_guard<std::mutex> lock(_mutex);
    EncryptionKey sleeve_key;
    if (!IsUnlocked(sleeve_key))
    {
        LOG_ERROR(_log) << "Sleeve::GetECIESKey - Sleeve is locked.";
        return nullptr;
    }

    CipherText cipher;
    IV iv;
    if (!AEEntryGet(_ecies_locator, cipher, iv, tx))
    {
        LOG_ERROR(_log) << "Sleeve::GetECIESKey - entry does not exist.";
        return nullptr;
    }

    PlainText ecies_prv_raw;
    if (!AuthenticatedDecrypt(cipher, ecies_prv_raw, sleeve_key, iv))
    {
        LOG_ERROR(_log) << "Sleeve::GetECIESKey - cannot decrypt ECIES private key.";
        return nullptr;
    }

    return std::make_unique<ECIESKeyPair>(ecies_prv_raw);
}

void Sleeve::VersionPut(logos::transaction const & tx, unsigned version)
{
    Byte32Array entry(version);
    EntryPutRaw(_version_locator, logos::mdb_val(entry), tx);
}

void Sleeve::AEEntryPut(Byte32Array const & db_key, CipherText const & cipher, IV const & iv, logos::transaction const & tx)
{
    std::vector<uint8_t> buf;
    {
        logos::vectorstream stream(buf);  /*dtor flushes to buf*/
        logos::write(stream, cipher);
        logos::write(stream, iv);
    }
    EntryPutRaw(db_key, logos::mdb_val(buf.size(), buf.data()), tx);
}

bool Sleeve::AEEntryGet(Byte32Array const & db_key, CipherText & cipher, IV & iv, logos::transaction const & tx)
{
    logos::mdb_val value;

    auto status(mdb_get(tx, _sleeve_handle, logos::mdb_val(db_key), value));
    assert (status == 0 || status == MDB_NOTFOUND);

    if (status == MDB_NOTFOUND)
    {
        LOG_ERROR(_log) << "Sleeve::AEEntryGet - entry not found.";
        return false;
    }

    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(value.data()), value.size());
    if (logos::read(stream, cipher))
    {
        LOG_ERROR(_log) << "Sleeve::AEEntryGet - error reading cipher text.";
        return false;
    }
    if (logos::read(stream, iv))
    {
        LOG_ERROR(_log) << "Sleeve::AEEntryGet - error reading IV.";
        return false;
    }
    return true;
}

void Sleeve::EntryPutRaw(Byte32Array const & db_key, logos::mdb_val const & value, logos::transaction const & tx)
{
    auto error(mdb_put(tx.handle, _sleeve_handle, logos::mdb_val(db_key), value, 0));
    assert(!error);
}

bool Sleeve::EntryGet(Byte32Array const & db_key, Byte32Array & val, logos::transaction const & tx)
{
    logos::mdb_val value;
    auto status(mdb_get(tx.handle, _sleeve_handle, logos::mdb_val(db_key), value));

    assert (status == 0 || status == MDB_NOTFOUND);

    if (status == MDB_NOTFOUND)
    {
        return false;
    }

    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(value.data()), value.size());
    if (logos::read(stream, val))
    {
        LOG_ERROR(_log) << "Sleeve::EntryGet - error reading 32-byte data entry; suspected database corruption";
        return false;
    }
    return true;
}
