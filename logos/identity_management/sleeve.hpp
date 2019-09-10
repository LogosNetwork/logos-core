/// @file
/// This file contains the declaration of the Sleeve class,
/// which serves as a container for a node's sensitive delegate identity data.
/// The Sleeve handles key management operations for Logos governance identity.
/// Sleeve's key derivation and fan-out functions are also declared here.
///
#pragma once

#include <logos/consensus/messages/byte_arrays.hpp>
#include <logos/lib/ecies.hpp>
#include <logos/lib/log.hpp>
#include <logos/node/utility.hpp>

#include <bls/bls.hpp>

/// Fan-out data structure to spread a key out over the heap to
/// decrease the likelihood of it being recovered by memory inspection
template<size_t L>
class Fan final
{
public:
    /// Class constructor
    ///     @param[in] key to store
    ///     @param[in] fan-out size
    Fan(ByteArray<L> const &, size_t);

    /// Copies stored fan-out value to the provided raw key
    ///     @param[in] reference to raw key to copy value to
    void CopyValueTo(ByteArray<L> &);

    /// Store fan-out value from the provided raw key
    ///     @param[in] reference to raw key to set fan-out value from
    void SetValueFrom(ByteArray<L> const &);

    /// "Clear" fan-out value
    ///
    /// This function sets the fan value to 0 rather than all individual entries
    void Clear();

private:

    /// Helper function to retrieve stored fan-out value; not thread-safe
    ///     @param[in] reference to raw key to copy value to
    void RetrieveValue(ByteArray<L> &);

    std::mutex mutex;
    std::vector<std::unique_ptr<ByteArray<L>>> values;
};

class KDF final
{
public:
    void PHS (ByteArray<32> &, std::string const &, Byte32Array const &);

private:
    std::mutex mutex;
};


enum class sleeve_code
{
    success,                    // command successfully executed
    identity_control_disabled,  // identity control is disabled
    invalid_password,           // password is incorrect
    sleeve_locked,              // sleeve is locked
    sleeve_already_unlocked,    // sleeve cannot be unlocked twice
    already_sleeved,            // already sleeved but 'overwrite' is not set to true
    /// activation settings
    setting_already_applied,    // received command to activate / deactivate when already at desired setting
    already_scheduled,          // already scheduled for activation / deactivation
    nothing_scheduled,          // no future activation / deactivation scheduled
    invalid_setting_epoch,      // setting scheduled for an old epoch
    epoch_transition_started    // received scheduling command after the transition events for the specified epoch has already started
};

class sleeve_status
{
public:
    sleeve_status(sleeve_code code) : code(code) {}
    sleeve_code code;
    operator bool() const
    {
        return code == sleeve_code::success;
    }
};

std::string SleeveResultToString(sleeve_code result);

class Sleeve final
{
    friend class DelegateIdentityManager;
public:
    using BLSKeyPair    = bls::KeyPair;
    using EncryptionKey = ByteArray<AES256GCM_KEY_SIZE>;
    using IV            = ByteArray<AES256GCM_IV_SIZE>;

    /// Class constructor
    ///
    /// This constructor is called by node
    ///     @param[in] sleeve database name
    ///     @param[in] size of the fan-out data storage
    ///     @param[in] reference flag to indicate whether environment openning failed
    Sleeve(boost::filesystem::path const & path, unsigned fanout_size, bool & error);

    /// Populate the Sleeve database with initial content
    ///
    /// This function is called by both new database initialization and Sleeve reset command
    ///     @param[in] LMDB transaction wrapper
    void Initialize(logos::transaction const &);


    /// Changes the Sleeve's password, if Sleeve is unlocked
    ///
    ///     @param[in] password string
    ///     @param[in] LMDB transaction wrapper
    /// @return sleeve command status
    sleeve_status Rekey(std::string const &, logos::transaction const &);

    /// Check if Sleeve is locked by checking if in-memory cipher text of Sleeve key can be successfully decrypted
    ///
    /// @return true if successful, false otherwise
    bool IsUnlocked()
    {
        EncryptionKey ignored_sleeve_key;
        return IsUnlocked(ignored_sleeve_key);
    }

    /// Check if Sleeve is locked, writing raw sleeve key to provided param
    ///
    /// The caller of this function needs to acquire the sleeve mutex lock first!
    ///     @param[in|out] entry to write raw sleeve key to; care must be taken to not reveal sensitive data!
    /// @return true if Unlocked, false otherwise
    bool IsUnlocked(EncryptionKey &);

    /// Check if we are in Sleeved state, i.e. Unlocked and BLS and ECIES keys are stored in database
    ///
    /// The caller of this function needs to acquire the sleeve mutex lock first!
    /// @return true if Sleeved, false otherwise
    bool IsSleeved(logos::transaction const & tx)
    {
        return IsUnlocked() && KeysExist(tx);
    }

    static unsigned const kdf_full_work = 64 * 1024;
    static unsigned const kdf_test_work = 8;
    static unsigned const kdf_work = logos::logos_network ==logos::logos_networks::logos_test_network ? kdf_test_work : kdf_full_work;
    logos::mdb_env  _env;

protected:
    /// Use AES256-GCM to perform authenticated encryption on plaintext key
    ///
    ///     @param[in] plaintext to encrypt
    ///     @param[in|out] ciphertext to write encrypted data to
    ///     @param[in] symmetric AES encryption key
    ///     @param[in] initialization vector; must be new, otherwise the security would be compromised!
    static void AuthenticatedEncrypt(PlainText const &, CipherText &, EncryptionKey const &, IV const &);

    /// Use AES256-GCM to perform authenticated decryption on key ciphertext
    ///
    ///     @param[in] ciphertext to decrypt
    ///     @param[in|out] plaintext to write decrypted data to
    ///     @param[in] symmetric AES encryption key
    ///     @param[in] initialization vector
    static bool AuthenticatedDecrypt(CipherText const &, PlainText &, EncryptionKey const &, IV const &);

private:

    /// Attempts to unlock the Sleeve
    ///
    ///     @param[in] password string
    ///     @param[in] LMDB transaction wrapper
    /// @return sleeve command status
    sleeve_status Unlock(std::string const &, logos::transaction const &);

    /// Lock the Sleeve
    ///
    /// @return sleeve command status
    sleeve_status Lock();

    /// Stores governance keys in the Sleeve, if Sleeve is unlocked
    ///
    ///     This method is called by DelegateIdentityManager::Sleeve
    ///     @param[in] BLS private key in byte array
    ///     @param[in] ECIES private key in byte array
    ///     @param[in] boolean indicator of whether to overwrite existing stored content
    ///     @param[in] LMDB transaction wrapper
    /// @return sleeve command status
    sleeve_status StoreKeys(PlainText const &, PlainText const &, bool, logos::transaction const &);

    /// Unsleeve, erasing any stored keys
    ///
    ///     @param[in] LMDB transaction wrapper
    /// @return sleeve command status
    sleeve_status Unsleeve(logos::transaction const &);

    /// Reset Sleeve database, erasing the whole database and re-initializing the content
    ///
    ///     @param[in] LMDB transaction wrapper
    void Reset(logos::transaction const &);

    /// Derive encryption key from password using Argon2 hash
    ///
    /// This function is called by Initialize, Rekey, and Unlock
    ///     @param[in|out] value to write derived key to
    ///     @param[in] user password
    ///     @param[in] LMDB transaction wrapper
    void DeriveKey(EncryptionKey &, std::string const &, logos::transaction const &);

    /// Open Sleeve LMDB database
    ///
    ///     @param[in] LMDB transaction wrapper
    /// @return true if the database is successfully opened (new or existing), false otherwise
    bool OpenDB(logos::transaction const &);

    /// Check if the Sleeve DB contains content
    ///
    /// This function is called by the constructor to decide if initialization is needed.
    /// It checks whether the DB is empty by checking the stored version number as a proxy.
    ///     @param[in] LMDB transaction wrapper
    /// @return true if DB contains content, false if it's empty
    bool HasContent(logos::transaction const &);

    /// Check if BLS and ECIES keys are store in database
    ///
    ///     @param[in] LMDB transaction wrapper
    /// @return true if both exist, false otherwise
    bool KeysExist(logos::transaction const &);

    /// Retrieves BLS key pair and stores in passed-in reference
    ///
    ///     @param[in] LMDB transaction wrapper
    /// @return shared pointer to BLSKeyPair if found, nullptr otherwise
    std::unique_ptr<BLSKeyPair> GetBLSKey(logos::transaction const &);

    /// Retrieves BLS key pair and stores in passed-in reference
    ///
    ///     @param[in] LMDB transaction wrapper
    /// @return shared pointer to ECIESKeyPair if found, nullptr otherwise
    std::unique_ptr<ECIESKeyPair> GetECIESKey(logos::transaction const &);

    /// Store Sleeve version to database
    ///
    /// This function is only called by `HasContent` during the initialization phase of a new database.
    /// The version entry also serves as a placeholder indicating the database has been previously created.
    ///
    void VersionPut(logos::transaction const &, unsigned);

    /// Store authenticated encryption entry (ciphertext, iv) to database.
    /// The ciphertext includes the MAC.
    ///
    ///     @param[in] LMDB key
    ///     @param[in] ciphertext to be stored
    ///     @param[in] associated unique IV to be stored
    ///     @param[in] LMDB transaction wrapper
    void AEEntryPut(Byte32Array const &, CipherText const &, IV const &, logos::transaction const &);

    /// Retrieve authenticated encryption entry (ciphertext, iv) from database.
    /// The ciphertext includes the MAC.
    ///
    ///     @param[in] LMDB key
    ///     @param[in|out] ciphertext to be retrieved
    ///     @param[in|out] associated unique IV to be retrieved
    ///     @param[in] LMDB transaction wrapper
    /// @return true on success, false otherwise
    bool AEEntryGet(Byte32Array const &, CipherText &, IV &, logos::transaction const &);

    /// Store entry to database
    ///
    ///     @param[in] LMDB key
    ///     @param[in] LMDB value
    ///     @param[in] LMDB transaction wrapper
    void EntryPutRaw(Byte32Array const &, logos::mdb_val const &, logos::transaction const &);

    /// Retrieve entry from database
    ///
    ///     @parama[in] LMDB key
    ///     @param[in|out] LMDB value to be retrieved
    ///     @param[in] LMDB transaction wrapper
    /// @return true on success and false otherwise
    bool EntryGet(Byte32Array const &, Byte32Array &, logos::transaction const &);

    Fan<PL>         _password;
    Fan<CL>         _sleeve_key_cipher;
    IV              _sleeve_key_iv;
    KDF             _kdf;
    MDB_dbi         _sleeve_handle;
    std::mutex      _mutex;
    Log             _log;

    unsigned const _version_current = 0;
    static Byte32Array const _version_locator;    ///< Sleeve version number
    static Byte32Array const _sleeve_key_locator; ///< Key used to encrypt governance keys, encrypted itself by the user password
    static Byte32Array const _salt_locator;       ///< Random number used to salt private key encryption
    static Byte32Array const _bls_locator;        ///< BLS key pair
    static Byte32Array const _ecies_locator;      ///< ECIES key pair

    friend class Sleeve_KeyEncryption_Test;
    friend class TestNode;
};
