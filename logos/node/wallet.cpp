#include <logos/node/wallet.hpp>

#include <logos/lib/interface.h>
#include <logos/node/node.hpp>
#include <logos/node/xorshift.hpp>

#include <argon2.h>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <future>

#include <ed25519-donna/ed25519.h>

logos::uint256_union logos::wallet_store::check (MDB_txn * transaction_a)
{
    logos::wallet_value value (entry_get_raw (transaction_a, logos::wallet_store::check_special));
    return value.key;
}

logos::uint256_union logos::wallet_store::salt (MDB_txn * transaction_a)
{
    logos::wallet_value value (entry_get_raw (transaction_a, logos::wallet_store::salt_special));
    return value.key;
}

void logos::wallet_store::wallet_key (logos::raw_key & prv_a, MDB_txn * transaction_a)
{
    std::lock_guard<std::recursive_mutex> lock (mutex);
    logos::raw_key wallet_l;
    wallet_key_mem.value (wallet_l);
    logos::raw_key password_l;
    password.value (password_l);
    prv_a.decrypt (wallet_l.data, password_l, salt (transaction_a).owords[0]);
}

void logos::wallet_store::seed (logos::raw_key & prv_a, MDB_txn * transaction_a)
{
    logos::wallet_value value (entry_get_raw (transaction_a, logos::wallet_store::seed_special));
    logos::raw_key password_l;
    wallet_key (password_l, transaction_a);
    prv_a.decrypt (value.key, password_l, salt (transaction_a).owords[0]);
}

void logos::wallet_store::seed_set (MDB_txn * transaction_a, logos::raw_key const & prv_a)
{
    logos::raw_key password_l;
    wallet_key (password_l, transaction_a);
    logos::uint256_union ciphertext;
    ciphertext.encrypt (prv_a, password_l, salt (transaction_a).owords[0]);
    entry_put_raw (transaction_a, logos::wallet_store::seed_special, logos::wallet_value (ciphertext, 0));
    deterministic_clear (transaction_a);
}

logos::public_key logos::wallet_store::deterministic_insert (MDB_txn * transaction_a)
{
    auto index (deterministic_index_get (transaction_a));
    logos::raw_key prv;
    deterministic_key (prv, transaction_a, index);
    logos::public_key result;
    ed25519_publickey (prv.data.bytes.data (), result.bytes.data ());
    while (exists (transaction_a, result))
    {
        ++index;
        deterministic_key (prv, transaction_a, index);
        ed25519_publickey (prv.data.bytes.data (), result.bytes.data ());
    }
    uint64_t marker (1);
    marker <<= 32;
    marker |= index;
    entry_put_raw (transaction_a, result, logos::wallet_value (logos::uint256_union (marker), 0));
    ++index;
    deterministic_index_set (transaction_a, index);
    return result;
}

void logos::wallet_store::deterministic_key (logos::raw_key & prv_a, MDB_txn * transaction_a, uint32_t index_a)
{
    assert (valid_password (transaction_a));
    logos::raw_key seed_l;
    seed (seed_l, transaction_a);
    logos::deterministic_key (seed_l.data, index_a, prv_a.data);
}

uint32_t logos::wallet_store::deterministic_index_get (MDB_txn * transaction_a)
{
    logos::wallet_value value (entry_get_raw (transaction_a, logos::wallet_store::deterministic_index_special));
    return value.key.number ().convert_to<uint32_t> ();
}

void logos::wallet_store::deterministic_index_set (MDB_txn * transaction_a, uint32_t index_a)
{
    logos::uint256_union index_l (index_a);
    logos::wallet_value value (index_l, 0);
    entry_put_raw (transaction_a, logos::wallet_store::deterministic_index_special, value);
}

void logos::wallet_store::deterministic_clear (MDB_txn * transaction_a)
{
    logos::uint256_union key (0);
    for (auto i (begin (transaction_a)), n (end ()); i != n;)
    {
        switch (key_type (logos::wallet_value (i->second)))
        {
            case logos::key_type::deterministic:
            {
                logos::uint256_union key (i->first.uint256 ());
                erase (transaction_a, key);
                i = begin (transaction_a, key);
                break;
            }
            default:
            {
                ++i;
                break;
            }
        }
    }
    deterministic_index_set (transaction_a, 0);
}

bool logos::wallet_store::valid_password (MDB_txn * transaction_a)
{
    logos::raw_key zero;
    zero.data.clear ();
    logos::raw_key wallet_key_l;
    wallet_key (wallet_key_l, transaction_a);
    logos::uint256_union check_l;
    check_l.encrypt (zero, wallet_key_l, salt (transaction_a).owords[0]);
    bool ok = check (transaction_a) == check_l;
    return ok;
}

bool logos::wallet_store::attempt_password (MDB_txn * transaction_a, std::string const & password_a)
{
    bool result = false;
    {
        std::lock_guard<std::recursive_mutex> lock (mutex);
        logos::raw_key password_l;
        derive_key (password_l, transaction_a, password_a);
        password.value_set (password_l);
        result = !valid_password (transaction_a);
    }
    if (!result)
    {
        if (version (transaction_a) == version_1)
        {
            upgrade_v1_v2 ();
        }
        if (version (transaction_a) == version_2)
        {
            upgrade_v2_v3 ();
        }
    }
    return result;
}

bool logos::wallet_store::rekey (MDB_txn * transaction_a, std::string const & password_a)
{
    std::lock_guard<std::recursive_mutex> lock (mutex);
    bool result (false);
    if (valid_password (transaction_a))
    {
        logos::raw_key password_new;
        derive_key (password_new, transaction_a, password_a);
        logos::raw_key wallet_key_l;
        wallet_key (wallet_key_l, transaction_a);
        logos::raw_key password_l;
        password.value (password_l);
        password.value_set (password_new);
        logos::uint256_union encrypted;
        encrypted.encrypt (wallet_key_l, password_new, salt (transaction_a).owords[0]);
        logos::raw_key wallet_enc;
        wallet_enc.data = encrypted;
        wallet_key_mem.value_set (wallet_enc);
        entry_put_raw (transaction_a, logos::wallet_store::wallet_key_special, logos::wallet_value (encrypted, 0));
    }
    else
    {
        result = true;
    }
    return result;
}

void logos::wallet_store::derive_key (logos::raw_key & prv_a, MDB_txn * transaction_a, std::string const & password_a)
{
    auto salt_l (salt (transaction_a));
    kdf.phs (prv_a, password_a, salt_l);
}

logos::fan::fan (logos::uint256_union const & key, size_t count_a)
{
    std::unique_ptr<logos::uint256_union> first (new logos::uint256_union (key));
    for (auto i (1); i < count_a; ++i)
    {
        std::unique_ptr<logos::uint256_union> entry (new logos::uint256_union);
        random_pool.GenerateBlock (entry->bytes.data (), entry->bytes.size ());
        *first ^= *entry;
        values.push_back (std::move (entry));
    }
    values.push_back (std::move (first));
}

void logos::fan::value (logos::raw_key & prv_a)
{
    std::lock_guard<std::mutex> lock (mutex);
    value_get (prv_a);
}

void logos::fan::value_get (logos::raw_key & prv_a)
{
    assert (!mutex.try_lock ());
    prv_a.data.clear ();
    for (auto & i : values)
    {
        prv_a.data ^= *i;
    }
}

void logos::fan::value_set (logos::raw_key const & value_a)
{
    std::lock_guard<std::mutex> lock (mutex);
    logos::raw_key value_l;
    value_get (value_l);
    *(values[0]) ^= value_l.data;
    *(values[0]) ^= value_a.data;
}

logos::wallet_value::wallet_value (logos::mdb_val const & val_a)
{
    assert (val_a.size () == sizeof (*this));
    std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), key.chars.begin ());
    std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key) + sizeof (work), reinterpret_cast<char *> (&work));
}

logos::wallet_value::wallet_value (logos::uint256_union const & key_a, uint64_t work_a) :
key (key_a),
work (work_a)
{
}

logos::mdb_val logos::wallet_value::val () const
{
    static_assert (sizeof (*this) == sizeof (key) + sizeof (work), "Class not packed");
    return logos::mdb_val (sizeof (*this), const_cast<logos::wallet_value *> (this));
}

unsigned const logos::wallet_store::version_1 (1);
unsigned const logos::wallet_store::version_2 (2);
unsigned const logos::wallet_store::version_3 (3);
unsigned const logos::wallet_store::version_current (version_3);
// Wallet version number
logos::uint256_union const logos::wallet_store::version_special (0);
// Random number used to salt private key encryption
logos::uint256_union const logos::wallet_store::salt_special (1);
// Key used to encrypt wallet keys, encrypted itself by the user password
logos::uint256_union const logos::wallet_store::wallet_key_special (2);
// Check value used to see if password is valid
logos::uint256_union const logos::wallet_store::check_special (3);
// Representative account to be used if we open a new account
logos::uint256_union const logos::wallet_store::representative_special (4);
// Wallet seed for deterministic key generation
logos::uint256_union const logos::wallet_store::seed_special (5);
// Current key index for deterministic keys
logos::uint256_union const logos::wallet_store::deterministic_index_special (6);
int const logos::wallet_store::special_count (7);

logos::wallet_store::wallet_store (bool & init_a, logos::kdf & kdf_a, logos::transaction & transaction_a, logos::account representative_a, unsigned fanout_a, std::string const & wallet_a, std::string const & json_a) :
password (0, fanout_a),
wallet_key_mem (0, fanout_a),
kdf (kdf_a),
environment (transaction_a.environment)
{
    init_a = false;
    initialize (transaction_a, init_a, wallet_a);
    if (!init_a)
    {
        MDB_val junk;
        assert (mdb_get (transaction_a, handle, logos::mdb_val (version_special), &junk) == MDB_NOTFOUND);
        boost::property_tree::ptree wallet_l;
        std::stringstream istream (json_a);
        try
        {
            boost::property_tree::read_json (istream, wallet_l);
        }
        catch (...)
        {
            init_a = true;
        }
        for (auto i (wallet_l.begin ()), n (wallet_l.end ()); i != n; ++i)
        {
            logos::uint256_union key;
            init_a = key.decode_hex (i->first);
            if (!init_a)
            {
                logos::uint256_union value;
                init_a = value.decode_hex (wallet_l.get<std::string> (i->first));
                if (!init_a)
                {
                    entry_put_raw (transaction_a, key, logos::wallet_value (value, 0));
                }
                else
                {
                    init_a = true;
                }
            }
            else
            {
                init_a = true;
            }
        }
        init_a |= mdb_get (transaction_a, handle, logos::mdb_val (version_special), &junk) != 0;
        init_a |= mdb_get (transaction_a, handle, logos::mdb_val (wallet_key_special), &junk) != 0;
        init_a |= mdb_get (transaction_a, handle, logos::mdb_val (salt_special), &junk) != 0;
        init_a |= mdb_get (transaction_a, handle, logos::mdb_val (check_special), &junk) != 0;
        init_a |= mdb_get (transaction_a, handle, logos::mdb_val (representative_special), &junk) != 0;
        logos::raw_key key;
        key.data.clear ();
        password.value_set (key);
        key.data = entry_get_raw (transaction_a, logos::wallet_store::wallet_key_special).key;
        wallet_key_mem.value_set (key);
    }
}

logos::wallet_store::wallet_store (bool & init_a, logos::kdf & kdf_a, logos::transaction & transaction_a, logos::account representative_a, unsigned fanout_a, std::string const & wallet_a) :
password (0, fanout_a),
wallet_key_mem (0, fanout_a),
kdf (kdf_a),
environment (transaction_a.environment)
{
    init_a = false;
    initialize (transaction_a, init_a, wallet_a);
    if (!init_a)
    {
        int version_status;
        MDB_val version_value;
        version_status = mdb_get (transaction_a, handle, logos::mdb_val (version_special), &version_value);
        if (version_status == MDB_NOTFOUND)
        {
            version_put (transaction_a, version_current);
            logos::uint256_union salt_l;
            random_pool.GenerateBlock (salt_l.bytes.data (), salt_l.bytes.size ());
            entry_put_raw (transaction_a, logos::wallet_store::salt_special, logos::wallet_value (salt_l, 0));
            // Wallet key is a fixed random key that encrypts all entries
            logos::raw_key wallet_key;
            random_pool.GenerateBlock (wallet_key.data.bytes.data (), sizeof (wallet_key.data.bytes));
            logos::raw_key password_l;
            password_l.data.clear ();
            password.value_set (password_l);
            logos::raw_key zero;
            zero.data.clear ();
            // Wallet key is encrypted by the user's password
            logos::uint256_union encrypted;
            encrypted.encrypt (wallet_key, zero, salt_l.owords[0]);
            entry_put_raw (transaction_a, logos::wallet_store::wallet_key_special, logos::wallet_value (encrypted, 0));
            logos::raw_key wallet_key_enc;
            wallet_key_enc.data = encrypted;
            wallet_key_mem.value_set (wallet_key_enc);
            logos::uint256_union check;
            check.encrypt (zero, wallet_key, salt_l.owords[0]);
            entry_put_raw (transaction_a, logos::wallet_store::check_special, logos::wallet_value (check, 0));
            entry_put_raw (transaction_a, logos::wallet_store::representative_special, logos::wallet_value (representative_a, 0));
            logos::raw_key seed;
            random_pool.GenerateBlock (seed.data.bytes.data (), seed.data.bytes.size ());
            seed_set (transaction_a, seed);
            entry_put_raw (transaction_a, logos::wallet_store::deterministic_index_special, logos::wallet_value (logos::uint256_union (0), 0));
        }
    }
    logos::raw_key key;
    key.data = entry_get_raw (transaction_a, logos::wallet_store::wallet_key_special).key;
    wallet_key_mem.value_set (key);
}

std::vector<logos::account> logos::wallet_store::accounts (MDB_txn * transaction_a)
{
    std::vector<logos::account> result;
    for (auto i (begin (transaction_a)), n (end ()); i != n; ++i)
    {
        logos::account account (i->first.uint256 ());
        result.push_back (account);
    }
    return result;
}

void logos::wallet_store::initialize (MDB_txn * transaction_a, bool & init_a, std::string const & path_a)
{
    assert (strlen (path_a.c_str ()) == path_a.size ());
    auto error (0);
    error |= mdb_dbi_open (transaction_a, path_a.c_str (), MDB_CREATE, &handle);
    init_a = error != 0;
}

bool logos::wallet_store::is_representative (MDB_txn * transaction_a)
{
    return exists (transaction_a, representative (transaction_a));
}

void logos::wallet_store::representative_set (MDB_txn * transaction_a, logos::account const & representative_a)
{
    entry_put_raw (transaction_a, logos::wallet_store::representative_special, logos::wallet_value (representative_a, 0));
}

logos::account logos::wallet_store::representative (MDB_txn * transaction_a)
{
    logos::wallet_value value (entry_get_raw (transaction_a, logos::wallet_store::representative_special));
    return value.key;
}

logos::public_key logos::wallet_store::insert_adhoc (MDB_txn * transaction_a, logos::raw_key const & prv)
{
    assert (valid_password (transaction_a));
    logos::public_key pub;
    ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
    logos::raw_key password_l;
    wallet_key (password_l, transaction_a);
    logos::uint256_union ciphertext;
    ciphertext.encrypt (prv, password_l, salt (transaction_a).owords[0]);
    entry_put_raw (transaction_a, pub, logos::wallet_value (ciphertext, 0));
    return pub;
}

void logos::wallet_store::insert_watch (MDB_txn * transaction_a, logos::public_key const & pub)
{
    entry_put_raw (transaction_a, pub, logos::wallet_value (logos::uint256_union (0), 0));
}

void logos::wallet_store::erase (MDB_txn * transaction_a, logos::public_key const & pub)
{
    auto status (mdb_del (transaction_a, handle, logos::mdb_val (pub), nullptr));
    assert (status == 0);
}

logos::wallet_value logos::wallet_store::entry_get_raw (MDB_txn * transaction_a, logos::public_key const & pub_a)
{
    logos::wallet_value result;
    logos::mdb_val value;
    auto status (mdb_get (transaction_a, handle, logos::mdb_val (pub_a), value));
    if (status == 0)
    {
        result = logos::wallet_value (value);
    }
    else
    {
        result.key.clear ();
        result.work = 0;
    }
    return result;
}

void logos::wallet_store::entry_put_raw (MDB_txn * transaction_a, logos::public_key const & pub_a, logos::wallet_value const & entry_a)
{
    auto status (mdb_put (transaction_a, handle, logos::mdb_val (pub_a), entry_a.val (), 0));
    assert (status == 0);
}

logos::key_type logos::wallet_store::key_type (logos::wallet_value const & value_a)
{
    auto number (value_a.key.number ());
    logos::key_type result;
    auto text (number.convert_to<std::string> ());
    if (number > std::numeric_limits<uint64_t>::max ())
    {
        result = logos::key_type::adhoc;
    }
    else
    {
        if ((number >> 32).convert_to<uint32_t> () == 1)
        {
            result = logos::key_type::deterministic;
        }
        else
        {
            result = logos::key_type::unknown;
        }
    }
    return result;
}

bool logos::wallet_store::fetch (MDB_txn * transaction_a, logos::public_key const & pub, logos::raw_key & prv)
{
    auto result (false);
    if (valid_password (transaction_a))
    {
        logos::wallet_value value (entry_get_raw (transaction_a, pub));
        if (!value.key.is_zero ())
        {
            switch (key_type (value))
            {
                case logos::key_type::deterministic:
                {
                    logos::raw_key seed_l;
                    seed (seed_l, transaction_a);
                    uint32_t index (value.key.number ().convert_to<uint32_t> ());
                    deterministic_key (prv, transaction_a, index);
                    break;
                }
                case logos::key_type::adhoc:
                {
                    // Ad-hoc keys
                    logos::raw_key password_l;
                    wallet_key (password_l, transaction_a);
                    prv.decrypt (value.key, password_l, salt (transaction_a).owords[0]);
                    break;
                }
                default:
                {
                    result = true;
                    break;
                }
            }
        }
        else
        {
            result = true;
        }
    }
    else
    {
        result = true;
    }
    if (!result)
    {
        logos::public_key compare;
        ed25519_publickey (prv.data.bytes.data (), compare.bytes.data ());
        if (!(pub == compare))
        {
            result = true;
        }
    }
    return result;
}

bool logos::wallet_store::exists (MDB_txn * transaction_a, logos::public_key const & pub)
{
    return find (transaction_a, pub) != end ();
}

void logos::wallet_store::serialize_json (MDB_txn * transaction_a, std::string & string_a)
{
    boost::property_tree::ptree tree;
    for (logos::store_iterator i (transaction_a, handle), n (nullptr); i != n; ++i)
    {
        tree.put (logos::uint256_union (i->first.uint256 ()).to_string (), logos::wallet_value (i->second).key.to_string ());
    }
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, tree);
    string_a = ostream.str ();
}

void logos::wallet_store::write_backup (MDB_txn * transaction_a, boost::filesystem::path const & path_a)
{
    std::ofstream backup_file;
    backup_file.open (path_a.string ());
    if (!backup_file.fail ())
    {
        std::string json;
        serialize_json (transaction_a, json);
        backup_file << json;
    }
}

bool logos::wallet_store::move (MDB_txn * transaction_a, logos::wallet_store & other_a, std::vector<logos::public_key> const & keys)
{
    assert (valid_password (transaction_a));
    assert (other_a.valid_password (transaction_a));
    auto result (false);
    for (auto i (keys.begin ()), n (keys.end ()); i != n; ++i)
    {
        logos::raw_key prv;
        auto error (other_a.fetch (transaction_a, *i, prv));
        result = result | error;
        if (!result)
        {
            insert_adhoc (transaction_a, prv);
            other_a.erase (transaction_a, *i);
        }
    }
    return result;
}

bool logos::wallet_store::import (MDB_txn * transaction_a, logos::wallet_store & other_a)
{
    assert (valid_password (transaction_a));
    assert (other_a.valid_password (transaction_a));
    auto result (false);
    for (auto i (other_a.begin (transaction_a)), n (end ()); i != n; ++i)
    {
        logos::raw_key prv;
        auto error (other_a.fetch (transaction_a, i->first.uint256 (), prv));
        result = result | error;
        if (!result)
        {
            insert_adhoc (transaction_a, prv);
            other_a.erase (transaction_a, i->first.uint256 ());
        }
    }
    return result;
}

bool logos::wallet_store::work_get (MDB_txn * transaction_a, logos::public_key const & pub_a, uint64_t & work_a)
{
    auto result (false);
    auto entry (entry_get_raw (transaction_a, pub_a));
    if (!entry.key.is_zero ())
    {
        work_a = entry.work;
    }
    else
    {
        result = true;
    }
    return result;
}

void logos::wallet_store::work_put (MDB_txn * transaction_a, logos::public_key const & pub_a, uint64_t work_a)
{
    auto entry (entry_get_raw (transaction_a, pub_a));
    assert (!entry.key.is_zero ());
    entry.work = work_a;
    entry_put_raw (transaction_a, pub_a, entry);
}

unsigned logos::wallet_store::version (MDB_txn * transaction_a)
{
    logos::wallet_value value (entry_get_raw (transaction_a, logos::wallet_store::version_special));
    auto entry (value.key);
    auto result (static_cast<unsigned> (entry.bytes[31]));
    return result;
}

void logos::wallet_store::version_put (MDB_txn * transaction_a, unsigned version_a)
{
    logos::uint256_union entry (version_a);
    entry_put_raw (transaction_a, logos::wallet_store::version_special, logos::wallet_value (entry, 0));
}

void logos::wallet_store::upgrade_v1_v2 ()
{
    logos::transaction transaction (environment, nullptr, true);
    assert (version (transaction) == 1);
    logos::raw_key zero_password;
    logos::wallet_value value (entry_get_raw (transaction, logos::wallet_store::wallet_key_special));
    logos::raw_key kdf;
    kdf.data.clear ();
    zero_password.decrypt (value.key, kdf, salt (transaction).owords[0]);
    derive_key (kdf, transaction, "");
    logos::raw_key empty_password;
    empty_password.decrypt (value.key, kdf, salt (transaction).owords[0]);
    for (auto i (begin (transaction)), n (end ()); i != n; ++i)
    {
        logos::public_key key (i->first.uint256 ());
        logos::raw_key prv;
        if (fetch (transaction, key, prv))
        {
            // Key failed to decrypt despite valid password
            logos::wallet_value data (entry_get_raw (transaction, key));
            prv.decrypt (data.key, zero_password, salt (transaction).owords[0]);
            logos::public_key compare;
            ed25519_publickey (prv.data.bytes.data (), compare.bytes.data ());
            if (compare == key)
            {
                // If we successfully decrypted it, rewrite the key back with the correct wallet key
                insert_adhoc (transaction, prv);
            }
            else
            {
                // Also try the empty password
                logos::wallet_value data (entry_get_raw (transaction, key));
                prv.decrypt (data.key, empty_password, salt (transaction).owords[0]);
                logos::public_key compare;
                ed25519_publickey (prv.data.bytes.data (), compare.bytes.data ());
                if (compare == key)
                {
                    // If we successfully decrypted it, rewrite the key back with the correct wallet key
                    insert_adhoc (transaction, prv);
                }
            }
        }
    }
    version_put (transaction, 2);
}

void logos::wallet_store::upgrade_v2_v3 ()
{
    logos::transaction transaction (environment, nullptr, true);
    assert (version (transaction) == 2);
    logos::raw_key seed;
    random_pool.GenerateBlock (seed.data.bytes.data (), seed.data.bytes.size ());
    seed_set (transaction, seed);
    entry_put_raw (transaction, logos::wallet_store::deterministic_index_special, logos::wallet_value (logos::uint256_union (0), 0));
    version_put (transaction, 3);
}

void logos::kdf::phs (logos::raw_key & result_a, std::string const & password_a, logos::uint256_union const & salt_a)
{
    std::lock_guard<std::mutex> lock (mutex);
    auto success (argon2_hash (1, logos::wallet_store::kdf_work, 1, password_a.data (), password_a.size (), salt_a.bytes.data (), salt_a.bytes.size (), result_a.data.bytes.data (), result_a.data.bytes.size (), NULL, 0, Argon2_d, 0x10));
    assert (success == 0);
    (void)success;
}

logos::wallet::wallet (bool & init_a, logos::transaction & transaction_a, logos::node & node_a, std::string const & wallet_a) :
lock_observer ([](bool, bool) {}),
store (init_a, node_a.wallets.kdf, transaction_a, node_a.config.random_representative (), node_a.config.password_fanout, wallet_a),
node (node_a)
{
}

logos::wallet::wallet (bool & init_a, logos::transaction & transaction_a, logos::node & node_a, std::string const & wallet_a, std::string const & json) :
lock_observer ([](bool, bool) {}),
store (init_a, node_a.wallets.kdf, transaction_a, node_a.config.random_representative (), node_a.config.password_fanout, wallet_a, json),
node (node_a)
{
}

void logos::wallet::enter_initial_password ()
{
    logos::transaction transaction (store.environment, nullptr, true);
    std::lock_guard<std::recursive_mutex> lock (store.mutex);
    logos::raw_key password_l;
    store.password.value (password_l);
    if (password_l.data.is_zero ())
    {
        if (valid_password ())
        {
            // Newly created wallets have a zero key
            store.rekey (transaction, "");
        }
        enter_password ("");
    }
}

bool logos::wallet::valid_password ()
{
    logos::transaction transaction (store.environment, nullptr, false);
    auto result (store.valid_password (transaction));
    return result;
}

bool logos::wallet::enter_password (std::string const & password_a)
{
    logos::transaction transaction (store.environment, nullptr, false);
    auto result (store.attempt_password (transaction, password_a));
    if (!result)
    {
        auto this_l (shared_from_this ());
        node.background ([this_l]() {
            this_l->search_pending ();
        });
    }
    lock_observer (result, password_a.empty ());
    return result;
}

logos::public_key logos::wallet::deterministic_insert (MDB_txn * transaction_a, bool generate_work_a)
{
    logos::public_key key (0);
    if (store.valid_password (transaction_a))
    {
        key = store.deterministic_insert (transaction_a);
        if (generate_work_a)
        {
            work_ensure (key, key);
        }
    }
    return key;
}

logos::public_key logos::wallet::deterministic_insert (bool generate_work_a)
{
    logos::transaction transaction (store.environment, nullptr, true);
    auto result (deterministic_insert (transaction, generate_work_a));
    return result;
}

logos::public_key logos::wallet::insert_adhoc (MDB_txn * transaction_a, logos::raw_key const & key_a, bool generate_work_a)
{
    logos::public_key key (0);
    if (store.valid_password (transaction_a))
    {
        key = store.insert_adhoc (transaction_a, key_a);
        if (generate_work_a)
        {
            work_ensure (key, node.ledger.latest_root (transaction_a, key));
        }
    }
    return key;
}

logos::public_key logos::wallet::insert_adhoc (logos::raw_key const & account_a, bool generate_work_a)
{
    logos::transaction transaction (store.environment, nullptr, true);
    auto result (insert_adhoc (transaction, account_a, generate_work_a));
    return result;
}

void logos::wallet::insert_watch (MDB_txn * transaction_a, logos::public_key const & pub_a)
{
    store.insert_watch (transaction_a, pub_a);
}

bool logos::wallet::exists (logos::public_key const & account_a)
{
    logos::transaction transaction (store.environment, nullptr, false);
    return store.exists (transaction, account_a);
}

bool logos::wallet::import (std::string const & json_a, std::string const & password_a)
{
    auto error (false);
    std::unique_ptr<logos::wallet_store> temp;
    {
        logos::transaction transaction (store.environment, nullptr, true);
        logos::uint256_union id;
        random_pool.GenerateBlock (id.bytes.data (), id.bytes.size ());
        temp.reset (new logos::wallet_store (error, node.wallets.kdf, transaction, 0, 1, id.to_string (), json_a));
    }
    if (!error)
    {
        logos::transaction transaction (store.environment, nullptr, false);
        error = temp->attempt_password (transaction, password_a);
    }
    logos::transaction transaction (store.environment, nullptr, true);
    if (!error)
    {
        error = store.import (transaction, *temp);
    }
    temp->destroy (transaction);
    return error;
}

void logos::wallet::serialize (std::string & json_a)
{
    logos::transaction transaction (store.environment, nullptr, false);
    store.serialize_json (transaction, json_a);
}

void logos::wallet_store::destroy (MDB_txn * transaction_a)
{
    auto status (mdb_drop (transaction_a, handle, 1));
    assert (status == 0);
}

std::shared_ptr<logos::block> logos::wallet::receive_action (logos::block const & send_a, logos::account const & representative_a, logos::uint128_union const & amount_a, bool generate_work_a)
{
    logos::account account;
    auto hash (send_a.hash ());
    std::shared_ptr<logos::block> block;
    if (node.config.receive_minimum.number () <= amount_a.number ())
    {
        logos::transaction transaction (node.ledger.store.environment, nullptr, false);
        logos::pending_info pending_info;
        if (node.store.block_exists (transaction, hash))
        {
            account = node.ledger.block_destination (transaction, send_a);
            if (!node.ledger.store.pending_get (transaction, logos::pending_key (account, hash), pending_info))
            {
                logos::raw_key prv;
                if (!store.fetch (transaction, account, prv))
                {
                    uint64_t cached_work (0);
                    store.work_get (transaction, account, cached_work);
                    logos::account_info info;
                    auto new_account (node.ledger.store.account_get (transaction, account, info));
                    if (!new_account)
                    {
                        std::shared_ptr<logos::block> rep_block = node.ledger.store.block_get (transaction, info.rep_block);
                        assert (rep_block != nullptr);
                        if (should_generate_state_block (transaction, info.head))
                        {
                            block.reset (new logos::state_block (account, info.head, rep_block->representative (), info.balance.number () + pending_info.amount.number (), 0, hash, prv, account, cached_work));
                        }

                    }
                    else
                    {
                        if (node.ledger.state_block_generation_enabled (transaction))
                        {
                            block.reset (new logos::state_block (account, 0, representative_a, pending_info.amount, 0, hash, prv, account, cached_work));
                        }

                    }
                }
                else
                {
                    BOOST_LOG (node.log) << "Unable to receive, wallet locked";
                }
            }
            else
            {
                // Ledger doesn't have this marked as available to receive anymore
            }
        }
        else
        {
            // Ledger doesn't have this block anymore.
        }
    }
    else
    {
        BOOST_LOG (node.log) << boost::str (boost::format ("Not receiving block %1% due to minimum receive threshold") % hash.to_string ());
        // Someone sent us something below the threshold of receiving
    }
    if (block != nullptr)
    {
        if (logos::work_validate (*block))
        {
            node.work_generate_blocking (*block);
        }
        node.process_active (block);
        node.block_processor.flush ();
        if (generate_work_a)
        {
            work_ensure (account, block->hash ());
        }
    }
    return block;
}

std::shared_ptr<logos::block> logos::wallet::change_action (logos::account const & source_a, logos::account const & representative_a, bool generate_work_a)
{
    std::shared_ptr<logos::block> block;
    {
        logos::transaction transaction (store.environment, nullptr, false);
        if (store.valid_password (transaction))
        {
            auto existing (store.find (transaction, source_a));
            if (existing != store.end () && !node.ledger.latest (transaction, source_a).is_zero ())
            {
                logos::account_info info;
                auto error1 (node.ledger.store.account_get (transaction, source_a, info));
                assert (!error1);
                logos::raw_key prv;
                auto error2 (store.fetch (transaction, source_a, prv));
                assert (!error2);
                uint64_t cached_work (0);
                store.work_get (transaction, source_a, cached_work);
                if (should_generate_state_block (transaction, info.head))
                {
                    block.reset (new logos::state_block (source_a, info.head, representative_a, info.balance, 0, 0, prv, source_a, cached_work));
                }

            }
        }
    }
    if (block != nullptr)
    {
        if (logos::work_validate (*block))
        {
            node.work_generate_blocking (*block);
        }
        node.process_active (block);
        node.block_processor.flush ();
        if (generate_work_a)
        {
            work_ensure (source_a, block->hash ());
        }
    }
    return block;
}

std::shared_ptr<logos::block> logos::wallet::send_action (logos::account const & source_a, logos::account const & account_a, logos::uint128_t const & amount_a, bool generate_work_a, boost::optional<std::string> id_a)
{
    std::shared_ptr<logos::block> block;
    boost::optional<logos::mdb_val> id_mdb_val;
    if (id_a)
    {
        id_mdb_val = logos::mdb_val (id_a->size (), const_cast<char *> (id_a->data ()));
    }
    bool error = false;
    bool cached_block = false;
    {
        logos::transaction transaction (store.environment, nullptr, (bool)id_mdb_val);
        if (id_mdb_val)
        {
            logos::mdb_val result;
            auto status (mdb_get (transaction, node.wallets.send_action_ids, *id_mdb_val, result));
            if (status == 0)
            {
                auto hash (result.uint256 ());
                block = node.store.block_get (transaction, hash);
                if (block != nullptr)
                {
                    cached_block = true;
                    //CH-Code reduction
                    //node.network.republish_block (transaction, block);
                }
            }
            else if (status != MDB_NOTFOUND)
            {
                error = true;
            }
        }
        if (!error && block == nullptr)
        {
            if (store.valid_password (transaction))
            {
                auto existing (store.find (transaction, source_a));
                if (existing != store.end ())
                {
                    auto balance (node.ledger.account_balance (transaction, source_a));
                    if (!balance.is_zero () && balance >= amount_a)
                    {
                        logos::account_info info;
                        auto error1 (node.ledger.store.account_get (transaction, source_a, info));
                        assert (!error1);
                        logos::raw_key prv;
                        auto error2 (store.fetch (transaction, source_a, prv));
                        assert (!error2);
                        std::shared_ptr<logos::block> rep_block = node.ledger.store.block_get (transaction, info.rep_block);
                        assert (rep_block != nullptr);
                        uint64_t cached_work (0);
                        store.work_get (transaction, source_a, cached_work);
                        if (should_generate_state_block (transaction, info.head))
                        {
                            block.reset (new logos::state_block (source_a, info.head, rep_block->representative (), balance - amount_a, 0, account_a, prv, source_a, cached_work));
                        }
                        else
                        {
                            //CH block.reset (new logos::send_block (info.head, account_a, balance - amount_a, prv, source_a, cached_work));
                        }
                        if (id_mdb_val)
                        {
                            auto status (mdb_put (transaction, node.wallets.send_action_ids, *id_mdb_val, logos::mdb_val (block->hash ()), 0));
                            if (status != 0)
                            {
                                block = nullptr;
                                error = true;
                            }
                        }
                    }
                }
            }
        }
    }
    if (!error && block != nullptr && !cached_block)
    {
        if (logos::work_validate (*block))
        {
            node.work_generate_blocking (*block);
        }
        node.process_active (block);
        node.block_processor.flush ();
        if (generate_work_a)
        {
            work_ensure (source_a, block->hash ());
        }
    }
    return block;
}

bool logos::wallet::should_generate_state_block (MDB_txn * transaction_a, logos::block_hash const & hash_a)
{
    /*auto head (node.store.block_get (transaction_a, hash_a));
    assert (head != nullptr);
    auto is_state (dynamic_cast<logos::state_block *> (head.get ()) != nullptr);
    return is_state || node.ledger.state_block_generation_enabled (transaction_a);*/
    return true;
}

bool logos::wallet::change_sync (logos::account const & source_a, logos::account const & representative_a)
{
    std::promise<bool> result;
    change_async (source_a, representative_a, [&result](std::shared_ptr<logos::block> block_a) {
        result.set_value (block_a == nullptr);
    },
    true);
    return result.get_future ().get ();
}

void logos::wallet::change_async (logos::account const & source_a, logos::account const & representative_a, std::function<void(std::shared_ptr<logos::block>)> const & action_a, bool generate_work_a)
{
    node.wallets.queue_wallet_action (logos::wallets::high_priority, [this, source_a, representative_a, action_a, generate_work_a]() {
        auto block (change_action (source_a, representative_a, generate_work_a));
        action_a (block);
    });
}

bool logos::wallet::receive_sync (std::shared_ptr<logos::block> block_a, logos::account const & representative_a, logos::uint128_t const & amount_a)
{
    std::promise<bool> result;
    receive_async (block_a, representative_a, amount_a, [&result](std::shared_ptr<logos::block> block_a) {
        result.set_value (block_a == nullptr);
    },
    true);
    return result.get_future ().get ();
}

void logos::wallet::receive_async (std::shared_ptr<logos::block> block_a, logos::account const & representative_a, logos::uint128_t const & amount_a, std::function<void(std::shared_ptr<logos::block>)> const & action_a, bool generate_work_a)
{
    //assert (dynamic_cast<logos::send_block *> (block_a.get ()) != nullptr);
    node.wallets.queue_wallet_action (amount_a, [this, block_a, representative_a, amount_a, action_a, generate_work_a]() {
        auto block (receive_action (*static_cast<logos::block *> (block_a.get ()), representative_a, amount_a, generate_work_a));
        action_a (block);
    });
}

logos::block_hash logos::wallet::send_sync (logos::account const & source_a, logos::account const & account_a, logos::uint128_t const & amount_a)
{
    std::promise<logos::block_hash> result;
    send_async (source_a, account_a, amount_a, [&result](std::shared_ptr<logos::block> block_a) {
        result.set_value (block_a->hash ());
    },
    true);
    return result.get_future ().get ();
}

void logos::wallet::send_async (logos::account const & source_a, logos::account const & account_a, logos::uint128_t const & amount_a, std::function<void(std::shared_ptr<logos::block>)> const & action_a, bool generate_work_a, boost::optional<std::string> id_a)
{
    this->node.wallets.queue_wallet_action (logos::wallets::high_priority, [this, source_a, account_a, amount_a, action_a, generate_work_a, id_a]() {
        auto block (send_action (source_a, account_a, amount_a, generate_work_a, id_a));
        action_a (block);
    });
}

// Update work for account if latest root is root_a
void logos::wallet::work_update (MDB_txn * transaction_a, logos::account const & account_a, logos::block_hash const & root_a, uint64_t work_a)
{
    assert (!logos::work_validate (root_a, work_a));
    assert (store.exists (transaction_a, account_a));
    auto latest (node.ledger.latest_root (transaction_a, account_a));
    if (latest == root_a)
    {
        store.work_put (transaction_a, account_a, work_a);
    }
    else
    {
        BOOST_LOG (node.log) << "Cached work no longer valid, discarding";
    }
}

void logos::wallet::work_ensure (logos::account const & account_a, logos::block_hash const & hash_a)
{
    auto this_l (shared_from_this ());
    node.wallets.queue_wallet_action (logos::wallets::generate_priority, [this_l, account_a, hash_a] {
        this_l->work_cache_blocking (account_a, hash_a);
    });
}

bool logos::wallet::search_pending ()
{
    logos::transaction transaction (store.environment, nullptr, false);
    auto result (!store.valid_password (transaction));
    if (!result)
    {
        BOOST_LOG (node.log) << "Beginning pending block search";
        logos::transaction transaction (node.store.environment, nullptr, false);
        for (auto i (store.begin (transaction)), n (store.end ()); i != n; ++i)
        {
            logos::account account (i->first.uint256 ());
            // Don't search pending for watch-only accounts
            if (!logos::wallet_value (i->second).key.is_zero ())
            {
                for (auto j (node.store.pending_begin (transaction, logos::pending_key (account, 0))), m (node.store.pending_begin (transaction, logos::pending_key (account.number () + 1, 0))); j != m; ++j)
                {
                    logos::pending_key key (j->first);
                    auto hash (key.hash);
                    logos::pending_info pending (j->second);
                    auto amount (pending.amount.number ());
                    if (node.config.receive_minimum.number () <= amount)
                    {
                        BOOST_LOG (node.log) << boost::str (boost::format ("Found a pending block %1% for account %2%") % hash.to_string () % pending.source.to_account ());
                        auto this_l (shared_from_this ());
                        logos::account_info info;
                        auto error (node.store.account_get (transaction, pending.source, info));
                        assert (!error);
                        //CH node.block_confirm (node.store.block_get (transaction, info.head));
                    }
                }
            }
        }
        BOOST_LOG (node.log) << "Pending block search phase complete";
    }
    else
    {
        BOOST_LOG (node.log) << "Stopping search, wallet is locked";
    }
    return result;
}

void logos::wallet::init_free_accounts (MDB_txn * transaction_a)
{
    free_accounts.clear ();
    for (auto i (store.begin (transaction_a)), n (store.end ()); i != n; ++i)
    {
        free_accounts.insert (i->first.uint256 ());
    }
}

logos::public_key logos::wallet::change_seed (MDB_txn * transaction_a, logos::raw_key const & prv_a)
{
    store.seed_set (transaction_a, prv_a);
    auto account = deterministic_insert (transaction_a);
    uint32_t count (0);
    for (uint32_t i (1), n (64); i < n; ++i)
    {
        logos::raw_key prv;
        store.deterministic_key (prv, transaction_a, i);
        logos::keypair pair (prv.data.to_string ());
        // Check if account received at least 1 block
        auto latest (node.ledger.latest (transaction_a, pair.pub));
        if (!latest.is_zero ())
        {
            count = i;
            // i + 64 - Check additional 64 accounts
            // i/64 - Check additional accounts for large wallets. I.e. 64000/64 = 1000 accounts to check
            n = i + 64 + (i / 64);
        }
        else
        {
            // Check if there are pending blocks for account
            logos::account end (pair.pub.number () + 1);
            for (auto ii (node.store.pending_begin (transaction_a, logos::pending_key (pair.pub, 0))), nn (node.store.pending_begin (transaction_a, logos::pending_key (end, 0))); ii != nn; ++ii)
            {
                count = i;
                n = i + 64 + (i / 64);
                break;
            }
        }
    }
    for (uint32_t i (0); i < count; ++i)
    {
        // Generate work for first 4 accounts only to prevent weak CPU nodes stuck
        account = deterministic_insert (transaction_a, i < 4);
    }

    return account;
}

void logos::wallet::work_cache_blocking (logos::account const & account_a, logos::block_hash const & root_a)
{
    auto begin (std::chrono::steady_clock::now ());
    auto work (node.work_generate_blocking (root_a));
    if (node.config.logging.work_generation_time ())
    {
        BOOST_LOG (node.log) << "Work generation complete: " << (std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::steady_clock::now () - begin).count ()) << " us";
    }
    logos::transaction transaction (store.environment, nullptr, true);
    if (store.exists (transaction, account_a))
    {
        work_update (transaction, account_a, root_a, work);
    }
}

logos::wallets::wallets (bool & error_a, logos::node & node_a) :
observer ([](bool) {}),
node (node_a),
stopped (false),
thread ([this]() { do_wallet_actions (); })
{
    if (!error_a)
    {
        logos::transaction transaction (node.store.environment, nullptr, true);
        auto status (mdb_dbi_open (transaction, nullptr, MDB_CREATE, &handle));
        status |= mdb_dbi_open (transaction, "send_action_ids", MDB_CREATE, &send_action_ids);
        assert (status == 0);
        std::string beginning (logos::uint256_union (0).to_string ());
        std::string end ((logos::uint256_union (logos::uint256_t (0) - logos::uint256_t (1))).to_string ());
        for (logos::store_iterator i (transaction, handle, logos::mdb_val (beginning.size (), const_cast<char *> (beginning.c_str ()))), n (transaction, handle, logos::mdb_val (end.size (), const_cast<char *> (end.c_str ()))); i != n; ++i)
        {
            logos::uint256_union id;
            std::string text (reinterpret_cast<char const *> (i->first.data ()), i->first.size ());
            auto error (id.decode_hex (text));
            assert (!error);
            assert (items.find (id) == items.end ());
            auto wallet (std::make_shared<logos::wallet> (error, transaction, node_a, text));
            if (!error)
            {
                node_a.background ([wallet]() {
                    wallet->enter_initial_password ();
                });
                items[id] = wallet;
            }
            else
            {
                // Couldn't open wallet
            }
        }
    }
}

logos::wallets::~wallets ()
{
    stop ();
}

std::shared_ptr<logos::wallet> logos::wallets::open (logos::uint256_union const & id_a)
{
    std::shared_ptr<logos::wallet> result;
    auto existing (items.find (id_a));
    if (existing != items.end ())
    {
        result = existing->second;
    }
    return result;
}

std::shared_ptr<logos::wallet> logos::wallets::create (logos::uint256_union const & id_a)
{
    assert (items.find (id_a) == items.end ());
    std::shared_ptr<logos::wallet> result;
    bool error;
    {
        logos::transaction transaction (node.store.environment, nullptr, true);
        result = std::make_shared<logos::wallet> (error, transaction, node, id_a.to_string ());
    }
    if (!error)
    {
        items[id_a] = result;
        node.background ([result]() {
            result->enter_initial_password ();
        });
    }
    return result;
}

bool logos::wallets::search_pending (logos::uint256_union const & wallet_a)
{
    auto result (false);
    auto existing (items.find (wallet_a));
    result = existing == items.end ();
    if (!result)
    {
        auto wallet (existing->second);
        result = wallet->search_pending ();
    }
    return result;
}

void logos::wallets::search_pending_all ()
{
    for (auto i : items)
    {
        i.second->search_pending ();
    }
}

void logos::wallets::destroy (logos::uint256_union const & id_a)
{
    logos::transaction transaction (node.store.environment, nullptr, true);
    auto existing (items.find (id_a));
    assert (existing != items.end ());
    auto wallet (existing->second);
    items.erase (existing);
    wallet->store.destroy (transaction);
}

void logos::wallets::do_wallet_actions ()
{
    std::unique_lock<std::mutex> lock (mutex);
    while (!stopped)
    {
        if (!actions.empty ())
        {
            auto first (actions.begin ());
            auto current (std::move (first->second));
            actions.erase (first);
            lock.unlock ();
            observer (true);
            current ();
            observer (false);
            lock.lock ();
        }
        else
        {
            condition.wait (lock);
        }
    }
}

void logos::wallets::queue_wallet_action (logos::uint128_t const & amount_a, std::function<void()> const & action_a)
{
    std::lock_guard<std::mutex> lock (mutex);
    actions.insert (std::make_pair (amount_a, std::move (action_a)));
    condition.notify_all ();
}

void logos::wallets::foreach_representative (MDB_txn * transaction_a, std::function<void(logos::public_key const & pub_a, logos::raw_key const & prv_a)> const & action_a)
{
    for (auto i (items.begin ()), n (items.end ()); i != n; ++i)
    {
        auto & wallet (*i->second);
        for (auto j (wallet.store.begin (transaction_a)), m (wallet.store.end ()); j != m; ++j)
        {
            logos::account account (j->first.uint256 ());
            if (!node.ledger.weight (transaction_a, account).is_zero ())
            {
                if (wallet.store.valid_password (transaction_a))
                {
                    logos::raw_key prv;
                    auto error (wallet.store.fetch (transaction_a, j->first.uint256 (), prv));
                    assert (!error);
                    action_a (j->first.uint256 (), prv);
                }
                else
                {
                    static auto last_log = std::chrono::steady_clock::time_point ();
                    if (last_log < std::chrono::steady_clock::now () - std::chrono::seconds (60))
                    {
                        last_log = std::chrono::steady_clock::now ();
                        BOOST_LOG (node.log) << boost::str (boost::format ("Representative locked inside wallet %1%") % i->first.to_string ());
                    }
                }
            }
        }
    }
}

bool logos::wallets::exists (MDB_txn * transaction_a, logos::public_key const & account_a)
{
    auto result (false);
    for (auto i (items.begin ()), n (items.end ()); !result && i != n; ++i)
    {
        result = i->second->store.exists (transaction_a, account_a);
    }
    return result;
}

void logos::wallets::stop ()
{
    {
        std::lock_guard<std::mutex> lock (mutex);
        stopped = true;
        condition.notify_all ();
    }
    if (thread.joinable ())
    {
        thread.join ();
    }
}

logos::uint128_t const logos::wallets::generate_priority = std::numeric_limits<logos::uint128_t>::max ();
logos::uint128_t const logos::wallets::high_priority = std::numeric_limits<logos::uint128_t>::max () - 1;

logos::store_iterator logos::wallet_store::begin (MDB_txn * transaction_a)
{
    logos::store_iterator result (transaction_a, handle, logos::mdb_val (logos::uint256_union (special_count)));
    return result;
}

logos::store_iterator logos::wallet_store::begin (MDB_txn * transaction_a, logos::uint256_union const & key)
{
    logos::store_iterator result (transaction_a, handle, logos::mdb_val (key));
    return result;
}

logos::store_iterator logos::wallet_store::find (MDB_txn * transaction_a, logos::uint256_union const & key)
{
    auto result (begin (transaction_a, key));
    logos::store_iterator end (nullptr);
    if (result != end)
    {
        if (logos::uint256_union (result->first.uint256 ()) == key)
        {
            return result;
        }
        else
        {
            return end;
        }
    }
    else
    {
        return end;
    }
    return result;
}

logos::store_iterator logos::wallet_store::end ()
{
    return logos::store_iterator (nullptr);
}
