#include <queue>
#include <logos/blockstore.hpp>
#include <logos/versioning.hpp>

namespace
{
/**
 * Fill in our predecessors
 */
class set_predecessor : public logos::block_visitor
{
public:
    set_predecessor (MDB_txn * transaction_a, logos::block_store & store_a) :
    transaction (transaction_a),
    store (store_a)
    {
    }
    virtual ~set_predecessor () = default;
    void fill_value (logos::block const & block_a)
    {
        auto hash (block_a.hash ());
        logos::block_type type;
        auto value (store.block_get_raw (transaction, block_a.previous (), type));
        assert (value.mv_size != 0);
        std::vector<uint8_t> data (static_cast<uint8_t *> (value.mv_data), static_cast<uint8_t *> (value.mv_data) + value.mv_size);
        std::copy (hash.bytes.begin (), hash.bytes.end (), data.end () - hash.bytes.size ());
        store.block_put_raw (transaction, store.block_database (type), block_a.previous (), logos::mdb_val (data.size (), data.data ()));
    }
    void state_block (logos::state_block const & block_a) override
    {
        if (!block_a.previous ().is_zero ())
        {
            fill_value (block_a);
        }
    }
    MDB_txn * transaction;
    logos::block_store & store;
};
}

logos::store_entry::store_entry () :
first (0, nullptr),
second (0, nullptr)
{
}

void logos::store_entry::clear ()
{
    first = { 0, nullptr };
    second = { 0, nullptr };
}

logos::store_entry * logos::store_entry::operator-> ()
{
    return this;
}

logos::store_entry & logos::store_iterator::operator-> ()
{
    return current;
}

logos::store_iterator::store_iterator (MDB_txn * transaction_a, MDB_dbi db_a) :
cursor (nullptr)
{
    auto status (mdb_cursor_open (transaction_a, db_a, &cursor));
    assert (status == 0);
    auto status2 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_FIRST));
    assert (status2 == 0 || status2 == MDB_NOTFOUND);
    if (status2 != MDB_NOTFOUND)
    {
        auto status3 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_GET_CURRENT));
        assert (status3 == 0 || status3 == MDB_NOTFOUND);
    }
    else
    {
        current.clear ();
    }
}

logos::store_iterator::store_iterator (std::nullptr_t) :
cursor (nullptr)
{
}

logos::store_iterator::store_iterator (MDB_txn * transaction_a, MDB_dbi db_a, MDB_val const & val_a) :
cursor (nullptr)
{
    auto status (mdb_cursor_open (transaction_a, db_a, &cursor));
    assert (status == 0);
    current.first.value = val_a;
    auto status2 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_SET_RANGE));
    assert (status2 == 0 || status2 == MDB_NOTFOUND);
    if (status2 != MDB_NOTFOUND)
    {
        auto status3 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_GET_CURRENT));
        assert (status3 == 0 || status3 == MDB_NOTFOUND);
    }
    else
    {
        current.clear ();
    }
}

logos::store_iterator::store_iterator (logos::store_iterator && other_a)
{
    cursor = other_a.cursor;
    other_a.cursor = nullptr;
    current = other_a.current;
}

logos::store_iterator::~store_iterator ()
{
    if (cursor != nullptr)
    {
        mdb_cursor_close (cursor);
    }
}

logos::store_iterator & logos::store_iterator::operator++ ()
{
    assert (cursor != nullptr);
    auto status (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_NEXT));
    if (status == MDB_NOTFOUND)
    {
        current.clear ();
    }
    return *this;
}

void logos::store_iterator::next_dup ()
{
    assert (cursor != nullptr);
    auto status (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_NEXT_DUP));
    if (status == MDB_NOTFOUND)
    {
        current.clear ();
    }
}

logos::store_iterator & logos::store_iterator::operator= (logos::store_iterator && other_a)
{
    if (cursor != nullptr)
    {
        mdb_cursor_close (cursor);
    }
    cursor = other_a.cursor;
    other_a.cursor = nullptr;
    current = other_a.current;
    other_a.current.clear ();
    return *this;
}

bool logos::store_iterator::operator== (logos::store_iterator const & other_a) const
{
    auto result (current.first.data () == other_a.current.first.data ());
    assert (!result || (current.first.size () == other_a.current.first.size ()));
    assert (!result || (current.second.data () == other_a.current.second.data ()));
    assert (!result || (current.second.size () == other_a.current.second.size ()));
    return result;
}

bool logos::store_iterator::operator!= (logos::store_iterator const & other_a) const
{
    return !(*this == other_a);
}

template<typename T>
void logos::block_store::put(MDB_dbi &db, const mdb_val &key, const T &t, MDB_txn *transaction)
{

    auto status(mdb_put(transaction, db, key, mdb_val(sizeof(T),
                                const_cast<T *>(&t)), 0));

    assert(status == 0);
}

template<typename T>
logos::block_hash logos::block_store::put(MDB_dbi &db, const T &t, MDB_txn *transaction)
{
    auto key = t.Hash();
    put<T>(db, key, t, transaction);
    return key;
}

template<typename T>
bool logos::block_store::get(MDB_dbi &db, const mdb_val &key, T &t)
{
    logos::mdb_val value;
    logos::transaction transaction(environment, nullptr, false);

    auto status (mdb_get (transaction, db, key, value));
    assert (status == 0 || status == MDB_NOTFOUND);
    bool result = false;
    if (status == MDB_NOTFOUND)
    {
        result = true;
    }
    else
    {
    memcpy((void*)&t, (void*)reinterpret_cast<T*> (value.data ()), value.size());
    }
  return result;
}

logos::store_iterator logos::block_store::block_info_begin (MDB_txn * transaction_a, logos::block_hash const & hash_a)
{
    logos::store_iterator result (transaction_a, blocks_info, logos::mdb_val (hash_a));
    return result;
}

logos::store_iterator logos::block_store::block_info_begin (MDB_txn * transaction_a)
{
    logos::store_iterator result (transaction_a, blocks_info);
    return result;
}

logos::store_iterator logos::block_store::block_info_end ()
{
    logos::store_iterator result (nullptr);
    return result;
}

logos::store_iterator logos::block_store::representation_begin (MDB_txn * transaction_a)
{
    logos::store_iterator result (transaction_a, representation);
    return result;
}

logos::store_iterator logos::block_store::representation_end ()
{
    logos::store_iterator result (nullptr);
    return result;
}

logos::store_iterator logos::block_store::unchecked_begin (MDB_txn * transaction_a)
{
    logos::store_iterator result (transaction_a, unchecked);
    return result;
}

logos::store_iterator logos::block_store::unchecked_begin (MDB_txn * transaction_a, logos::block_hash const & hash_a)
{
    logos::store_iterator result (transaction_a, unchecked, logos::mdb_val (hash_a));
    return result;
}

logos::store_iterator logos::block_store::unchecked_end ()
{
    logos::store_iterator result (nullptr);
    return result;
}

logos::store_iterator logos::block_store::vote_begin (MDB_txn * transaction_a)
{
    return logos::store_iterator (transaction_a, vote);
}

logos::store_iterator logos::block_store::vote_end ()
{
    return logos::store_iterator (nullptr);
}

logos::block_store::block_store (bool & error_a, boost::filesystem::path const & path_a, int lmdb_max_dbs) :
environment (error_a, path_a, lmdb_max_dbs),
frontiers (0),
accounts (0),
pending (0),
blocks_info (0),
representation (0),
unchecked (0),
checksum (0)
{
    if (!error_a)
    {
        logos::transaction transaction (environment, nullptr, true);

        // consensus-prototype
        error_a |= mdb_dbi_open (transaction, "batch_db", MDB_CREATE, &batch_db) != 0;
        error_a |= mdb_dbi_open (transaction, "state_db", MDB_CREATE, &state_db) != 0;
        error_a |= mdb_dbi_open (transaction, "account_db", MDB_CREATE, &account_db) != 0;
        error_a |= mdb_dbi_open (transaction, "receive_db", MDB_CREATE, &receive_db) != 0;
        error_a |= mdb_dbi_open (transaction, "batch_tips_db", MDB_CREATE, &batch_tips_db) != 0;

        // microblock-prototype
        error_a |= mdb_dbi_open (transaction, "micro_block_db", MDB_CREATE, &micro_block_db) != 0;
        error_a |= mdb_dbi_open (transaction, "micro_block_tip_db", MDB_CREATE, &micro_block_tip_db) != 0;

        error_a |= mdb_dbi_open (transaction, "frontiers", MDB_CREATE, &frontiers) != 0;
        error_a |= mdb_dbi_open (transaction, "accounts", MDB_CREATE, &accounts) != 0;
        error_a |= mdb_dbi_open (transaction, "state", MDB_CREATE, &state_blocks) != 0;
        error_a |= mdb_dbi_open (transaction, "pending", MDB_CREATE, &pending) != 0;
        error_a |= mdb_dbi_open (transaction, "blocks_info", MDB_CREATE, &blocks_info) != 0;
        error_a |= mdb_dbi_open (transaction, "representation", MDB_CREATE, &representation) != 0;
        error_a |= mdb_dbi_open (transaction, "unchecked", MDB_CREATE | MDB_DUPSORT, &unchecked) != 0;
        error_a |= mdb_dbi_open (transaction, "checksum", MDB_CREATE, &checksum) != 0;
        error_a |= mdb_dbi_open (transaction, "vote", MDB_CREATE, &vote) != 0;
        error_a |= mdb_dbi_open (transaction, "meta", MDB_CREATE, &meta) != 0;
        if (!error_a)
        {
            //CH do_upgrades (transaction);
            checksum_put (transaction, 0, 0, 0);
        }
    }
}

void logos::block_store::version_put (MDB_txn * transaction_a, int version_a)
{
    logos::uint256_union version_key (1);
    logos::uint256_union version_value (version_a);
    auto status (mdb_put (transaction_a, meta, logos::mdb_val (version_key), logos::mdb_val (version_value), 0));
    assert (status == 0);
}

int logos::block_store::version_get (MDB_txn * transaction_a)
{
    logos::uint256_union version_key (1);
    logos::mdb_val data;
    auto error (mdb_get (transaction_a, meta, logos::mdb_val (version_key), data));
    int result;
    if (error == MDB_NOTFOUND)
    {
        result = 1;
    }
    else
    {
        logos::uint256_union version_value (data.uint256 ());
        assert (version_value.qwords[2] == 0 && version_value.qwords[1] == 0 && version_value.qwords[0] == 0);
        result = version_value.number ().convert_to<int> ();
    }
    return result;
}

void logos::block_store::clear (MDB_dbi db_a)
{
    logos::transaction transaction (environment, nullptr, true);
    auto status (mdb_drop (transaction, db_a, 0));
    assert (status == 0);
}

logos::uint128_t logos::block_store::block_balance (MDB_txn * transaction_a, logos::block_hash const & hash_a)
{
    balance_visitor visitor (transaction_a, *this);
    visitor.compute (hash_a);
    return visitor.result;
}

void logos::block_store::representation_add (MDB_txn * transaction_a, logos::block_hash const & source_a, logos::uint128_t const & amount_a)
{
    auto source_block (block_get (transaction_a, source_a));
    assert (source_block != nullptr);
    auto source_rep (source_block->representative ());
    auto source_previous (representation_get (transaction_a, source_rep));
    representation_put (transaction_a, source_rep, source_previous + amount_a);
}

MDB_dbi logos::block_store::block_database (logos::block_type type_a)
{
    MDB_dbi result;
    switch (type_a)
    {
        case logos::block_type::state:
            result = state_blocks;
            break;
        default:
            assert (false);
            break;
    }
    return result;
}

void logos::block_store::block_put_raw (MDB_txn * transaction_a, MDB_dbi database_a, logos::block_hash const & hash_a, MDB_val value_a)
{
    auto status2 (mdb_put (transaction_a, database_a, logos::mdb_val (hash_a), &value_a, 0));
    assert (status2 == 0);
}

void logos::block_store::block_put (MDB_txn * transaction_a, logos::block_hash const & hash_a, logos::block const & block_a, logos::block_hash const & successor_a)
{
    assert (successor_a.is_zero () || block_exists (transaction_a, successor_a));
    std::vector<uint8_t> vector;
    {
        logos::vectorstream stream (vector);
        block_a.serialize (stream);
        logos::write (stream, successor_a.bytes);
    }
    block_put_raw (transaction_a, block_database (block_a.type ()), hash_a, { vector.size (), vector.data () });
    set_predecessor predecessor (transaction_a, *this);
    block_a.visit (predecessor);
    assert (block_a.previous ().is_zero () || block_successor (transaction_a, block_a.previous ()) == hash_a);
}

MDB_val logos::block_store::block_get_raw (MDB_txn * transaction_a, logos::block_hash const & hash_a, logos::block_type & type_a)
{
    logos::mdb_val result;

    auto status (mdb_get (transaction_a, state_blocks, logos::mdb_val (hash_a), result));
    assert (status == 0 || status == MDB_NOTFOUND);
    if (status != 0)
    {
        // Block not found
    }
    else
    {
        type_a = logos::block_type::state;
    }

    return result;
}

logos::block_hash logos::block_store::block_successor (MDB_txn * transaction_a, logos::block_hash const & hash_a)
{
    logos::block_type type;
    auto value (block_get_raw (transaction_a, hash_a, type));
    logos::block_hash result;
    if (value.mv_size != 0)
    {
        assert (value.mv_size >= result.bytes.size ());
        logos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data) + value.mv_size - result.bytes.size (), result.bytes.size ());
        auto error (logos::read (stream, result.bytes));
        assert (!error);
    }
    else
    {
        result.clear ();
    }
    return result;
}

void logos::block_store::block_successor_clear (MDB_txn * transaction_a, logos::block_hash const & hash_a)
{
    auto block (block_get (transaction_a, hash_a));
    block_put (transaction_a, hash_a, *block);
}

std::unique_ptr<logos::block> logos::block_store::block_get (MDB_txn * transaction_a, logos::block_hash const & hash_a)
{
    logos::block_type type;
    auto value (block_get_raw (transaction_a, hash_a, type));
    std::unique_ptr<logos::block> result;
    if (value.mv_size != 0)
    {
        logos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
        result = logos::deserialize_block (stream, type);
        assert (result != nullptr);
    }
    return result;
}

void logos::block_store::block_del (MDB_txn * transaction_a, logos::block_hash const & hash_a)
{
    auto status (mdb_del (transaction_a, state_blocks, logos::mdb_val (hash_a), nullptr));
    assert (status == 0);
}

bool logos::block_store::block_exists (MDB_txn * transaction_a, logos::block_hash const & hash_a)
{
    auto exists (true);
    logos::mdb_val junk;

    auto status (mdb_get (transaction_a, state_blocks, logos::mdb_val (hash_a), junk));
    assert (status == 0 || status == MDB_NOTFOUND);
    exists = status == 0;

    return exists;
}

logos::block_counts logos::block_store::block_count (MDB_txn * transaction_a)
{
    logos::block_counts result;

    MDB_stat state_stats;
    auto status5 (mdb_stat (transaction_a, state_blocks, &state_stats));
    assert (status5 == 0);
    result.state = state_stats.ms_entries;
    return result;
}

bool logos::block_store::root_exists (MDB_txn * transaction_a, logos::uint256_union const & root_a)
{
    return block_exists (transaction_a, root_a) || account_exists (transaction_a, root_a);
}

void logos::block_store::account_del (MDB_txn * transaction_a, logos::account const & account_a)
{
    auto status (mdb_del (transaction_a, accounts, logos::mdb_val (account_a), nullptr));
    assert (status == 0);
}

bool logos::block_store::account_exists (MDB_txn * transaction_a, logos::account const & account_a)
{
    auto iterator (latest_begin (transaction_a, account_a));
    return iterator != logos::store_iterator (nullptr) && logos::account (iterator->first.uint256 ()) == account_a;
}

bool logos::block_store::account_get (MDB_txn * transaction_a, logos::account const & account_a, logos::account_info & info_a)
{
    return account_get(transaction_a, account_a, info_a, accounts);
}

bool logos::block_store::account_get (MDB_txn * transaction_a, logos::account const & account_a, logos::account_info & info_a, MDB_dbi db)
{
    logos::mdb_val value;
    auto status (mdb_get (transaction_a, db, logos::mdb_val (account_a), value));
    assert (status == 0 || status == MDB_NOTFOUND);
    bool result;
    if (status == MDB_NOTFOUND)
    {
        result = true;
    }
    else
    {
        logos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
        result = info_a.deserialize (stream);
        assert (!result);
    }
    return result;
}

void logos::block_store::frontier_put (MDB_txn * transaction_a, logos::block_hash const & block_a, logos::account const & account_a)
{
    auto status (mdb_put (transaction_a, frontiers, logos::mdb_val (block_a), logos::mdb_val (account_a), 0));
    assert (status == 0);
}

logos::account logos::block_store::frontier_get (MDB_txn * transaction_a, logos::block_hash const & block_a)
{
    logos::mdb_val value;
    auto status (mdb_get (transaction_a, frontiers, logos::mdb_val (block_a), value));
    assert (status == 0 || status == MDB_NOTFOUND);
    logos::account result (0);
    if (status == 0)
    {
        result = value.uint256 ();
    }
    return result;
}

void logos::block_store::frontier_del (MDB_txn * transaction_a, logos::block_hash const & block_a)
{
    auto status (mdb_del (transaction_a, frontiers, logos::mdb_val (block_a), nullptr));
    assert (status == 0);
}

size_t logos::block_store::account_count (MDB_txn * transaction_a)
{
    MDB_stat frontier_stats;
    auto status (mdb_stat (transaction_a, accounts, &frontier_stats));
    assert (status == 0);
    auto result (frontier_stats.ms_entries);
    return result;
}

void logos::block_store::account_put (MDB_txn * transaction_a, logos::account const & account_a, logos::account_info const & info_a)
{
    auto status (mdb_put (transaction_a, accounts, logos::mdb_val (account_a), info_a.val (), 0));
    assert (status == 0);
}

void logos::block_store::pending_put (MDB_txn * transaction_a, logos::pending_key const & key_a, logos::pending_info const & pending_a)
{
    auto status (mdb_put (transaction_a, pending, key_a.val (), pending_a.val (), 0));
    assert (status == 0);
}

void logos::block_store::pending_del (MDB_txn * transaction_a, logos::pending_key const & key_a)
{
    auto status (mdb_del (transaction_a, pending, key_a.val (), nullptr));
    assert (status == 0);
}

bool logos::block_store::pending_exists (MDB_txn * transaction_a, logos::pending_key const & key_a)
{
    auto iterator (pending_begin (transaction_a, key_a));
    return iterator != logos::store_iterator (nullptr) && logos::pending_key (iterator->first) == key_a;
}

bool logos::block_store::pending_get (MDB_txn * transaction_a, logos::pending_key const & key_a, logos::pending_info & pending_a)
{
    logos::mdb_val value;
    auto status (mdb_get (transaction_a, pending, key_a.val (), value));
    assert (status == 0 || status == MDB_NOTFOUND);
    bool result;
    if (status == MDB_NOTFOUND)
    {
        result = true;
    }
    else
    {
        result = false;
        assert (value.size () == sizeof (pending_a.source.bytes) + sizeof (pending_a.amount.bytes));
        logos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
        auto error1 (logos::read (stream, pending_a.source));
        assert (!error1);
        auto error2 (logos::read (stream, pending_a.amount));
        assert (!error2);
    }
    return result;
}

logos::store_iterator logos::block_store::pending_begin (MDB_txn * transaction_a, logos::pending_key const & key_a)
{
    logos::store_iterator result (transaction_a, pending, key_a.val ());
    return result;
}

logos::store_iterator logos::block_store::pending_begin (MDB_txn * transaction_a)
{
    logos::store_iterator result (transaction_a, pending);
    return result;
}

logos::store_iterator logos::block_store::pending_end ()
{
    logos::store_iterator result (nullptr);
    return result;
}

void logos::block_store::block_info_put (MDB_txn * transaction_a, logos::block_hash const & hash_a, logos::block_info const & block_info_a)
{
    auto status (mdb_put (transaction_a, blocks_info, logos::mdb_val (hash_a), block_info_a.val (), 0));
    assert (status == 0);
}

void logos::block_store::block_info_del (MDB_txn * transaction_a, logos::block_hash const & hash_a)
{
    auto status (mdb_del (transaction_a, blocks_info, logos::mdb_val (hash_a), nullptr));
    assert (status == 0);
}

bool logos::block_store::block_info_exists (MDB_txn * transaction_a, logos::block_hash const & hash_a)
{
    auto iterator (block_info_begin (transaction_a, hash_a));
    return iterator != logos::store_iterator (nullptr) && logos::block_hash (iterator->first.uint256 ()) == hash_a;
}

bool logos::block_store::block_info_get (MDB_txn * transaction_a, logos::block_hash const & hash_a, logos::block_info & block_info_a)
{
    logos::mdb_val value;
    auto status (mdb_get (transaction_a, blocks_info, logos::mdb_val (hash_a), value));
    assert (status == 0 || status == MDB_NOTFOUND);
    bool result;
    if (status == MDB_NOTFOUND)
    {
        result = true;
    }
    else
    {
        result = false;
        assert (value.size () == sizeof (block_info_a.account.bytes) + sizeof (block_info_a.balance.bytes));
        logos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
        auto error1 (logos::read (stream, block_info_a.account));
        assert (!error1);
        auto error2 (logos::read (stream, block_info_a.balance));
        assert (!error2);
    }
    return result;
}

logos::uint128_t logos::block_store::representation_get (MDB_txn * transaction_a, logos::account const & account_a)
{
    logos::mdb_val value;
    auto status (mdb_get (transaction_a, representation, logos::mdb_val (account_a), value));
    assert (status == 0 || status == MDB_NOTFOUND);
    logos::uint128_t result;
    if (status == 0)
    {
        logos::uint128_union rep;
        logos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
        auto error (logos::read (stream, rep));
        assert (!error);
        result = rep.number ();
    }
    else
    {
        result = 0;
    }
    return result;
}

void logos::block_store::representation_put (MDB_txn * transaction_a, logos::account const & account_a, logos::uint128_t const & representation_a)
{
    logos::uint128_union rep (representation_a);
    auto status (mdb_put (transaction_a, representation, logos::mdb_val (account_a), logos::mdb_val (rep), 0));
    assert (status == 0);
}

void logos::block_store::unchecked_clear (MDB_txn * transaction_a)
{
    auto status (mdb_drop (transaction_a, unchecked, 0));
    assert (status == 0);
}

void logos::block_store::unchecked_put (MDB_txn * transaction_a, logos::block_hash const & hash_a, std::shared_ptr<logos::block> const & block_a)
{
    // Checking if same unchecked block is already in database
    bool exists (false);
    auto block_hash (block_a->hash ());
    auto cached (unchecked_get (transaction_a, hash_a));
    for (auto i (cached.begin ()), n (cached.end ()); i != n && !exists; ++i)
    {
        if ((*i)->hash () == block_hash)
        {
            exists = true;
        }
    }
    // Inserting block if it wasn't found in database
    if (!exists)
    {
        std::lock_guard<std::mutex> lock (cache_mutex);
        unchecked_cache.insert (std::make_pair (hash_a, block_a));
    }
}

std::shared_ptr<logos::vote> logos::block_store::vote_get (MDB_txn * transaction_a, logos::account const & account_a)
{
    std::shared_ptr<logos::vote> result;
    logos::mdb_val value;
    auto status (mdb_get (transaction_a, vote, logos::mdb_val (account_a), value));
    assert (status == 0 || status == MDB_NOTFOUND);
    if (status == 0)
    {
        result = std::make_shared<logos::vote> (value);
        assert (result != nullptr);
    }
    return result;
}

std::vector<std::shared_ptr<logos::block>> logos::block_store::unchecked_get (MDB_txn * transaction_a, logos::block_hash const & hash_a)
{
    std::vector<std::shared_ptr<logos::block>> result;
    {
        std::lock_guard<std::mutex> lock (cache_mutex);
        for (auto i (unchecked_cache.find (hash_a)), n (unchecked_cache.end ()); i != n && i->first == hash_a; ++i)
        {
            result.push_back (i->second);
        }
    }
    for (auto i (unchecked_begin (transaction_a, hash_a)), n (unchecked_end ()); i != n && logos::block_hash (i->first.uint256 ()) == hash_a; i.next_dup ())
    {
        logos::bufferstream stream (reinterpret_cast<uint8_t const *> (i->second.data ()), i->second.size ());
        result.push_back (logos::deserialize_block (stream));
    }
    return result;
}

void logos::block_store::unchecked_del (MDB_txn * transaction_a, logos::block_hash const & hash_a, logos::block const & block_a)
{
    {
        std::lock_guard<std::mutex> lock (cache_mutex);
        for (auto i (unchecked_cache.find (hash_a)), n (unchecked_cache.end ()); i != n && i->first == hash_a;)
        {
            if (*i->second == block_a)
            {
                i = unchecked_cache.erase (i);
            }
            else
            {
                ++i;
            }
        }
    }
    std::vector<uint8_t> vector;
    {
        logos::vectorstream stream (vector);
        logos::serialize_block (stream, block_a);
    }
    auto status (mdb_del (transaction_a, unchecked, logos::mdb_val (hash_a), logos::mdb_val (vector.size (), vector.data ())));
    assert (status == 0 || status == MDB_NOTFOUND);
}

size_t logos::block_store::unchecked_count (MDB_txn * transaction_a)
{
    MDB_stat unchecked_stats;
    auto status (mdb_stat (transaction_a, unchecked, &unchecked_stats));
    assert (status == 0);
    auto result (unchecked_stats.ms_entries);
    return result;
}

void logos::block_store::checksum_put (MDB_txn * transaction_a, uint64_t prefix, uint8_t mask, logos::uint256_union const & hash_a)
{
    assert ((prefix & 0xff) == 0);
    uint64_t key (prefix | mask);
    auto status (mdb_put (transaction_a, checksum, logos::mdb_val (sizeof (key), &key), logos::mdb_val (hash_a), 0));
    assert (status == 0);
}

bool logos::block_store::checksum_get (MDB_txn * transaction_a, uint64_t prefix, uint8_t mask, logos::uint256_union & hash_a)
{
    assert ((prefix & 0xff) == 0);
    uint64_t key (prefix | mask);
    logos::mdb_val value;
    auto status (mdb_get (transaction_a, checksum, logos::mdb_val (sizeof (key), &key), value));
    assert (status == 0 || status == MDB_NOTFOUND);
    bool result;
    if (status == 0)
    {
        result = false;
        logos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
        auto error (logos::read (stream, hash_a));
        assert (!error);
    }
    else
    {
        result = true;
    }
    return result;
}

void logos::block_store::checksum_del (MDB_txn * transaction_a, uint64_t prefix, uint8_t mask)
{
    assert ((prefix & 0xff) == 0);
    uint64_t key (prefix | mask);
    auto status (mdb_del (transaction_a, checksum, logos::mdb_val (sizeof (key), &key), nullptr));
    assert (status == 0);
}

logos::block_hash logos::block_store::batch_block_put (BatchStateBlock const & block, MDB_txn * transaction)
{
    auto result = block.Hash();

    auto status(mdb_put(transaction, batch_db, logos::mdb_val(result),
                        mdb_val(sizeof(BatchStateBlock),
                                const_cast<BatchStateBlock *>(&block)), 0));

    assert(status == 0);

    return result;
}

void logos::block_store::state_block_put(state_block const & block, StateBlockLocator const & locator, MDB_txn * transaction)
{
    auto status(mdb_put(transaction, state_db, logos::mdb_val(block.hash()),
                        mdb_val(sizeof(StateBlockLocator),
                                const_cast<StateBlockLocator *>(&locator)), 0));

    assert(status == 0);
}

bool logos::block_store::state_block_exists(const state_block & block)
{
    return state_block_exists(block.hash());
}

bool logos::block_store::state_block_exists(const block_hash & hash)
{
    logos::mdb_val junk;
    logos::transaction transaction(environment, nullptr, false);

    auto status (mdb_get (transaction, state_db, logos::mdb_val (hash), junk));
    assert (status == 0 || status == MDB_NOTFOUND);

    return status == 0;
}

bool logos::block_store::batch_block_get (const logos::block_hash &hash, BatchStateBlock & block)
{
  return get<BatchStateBlock>(batch_db, hash, block);
}

logos::block_hash logos::block_store::micro_block_put(MicroBlock const &block, MDB_txn *transaction)
{
    return put<MicroBlock>(micro_block_db, block, transaction);
}

bool logos::block_store::micro_block_get(logos::block_hash &hash, MicroBlock &block)
{
  return get<MicroBlock>(micro_block_db, mdb_val(hash), block);
}

void logos::block_store::micro_block_tip_put(const block_hash& hash, MDB_txn *transaction)
{
  const uint8_t key = 0; // only one tip
    put<block_hash>(micro_block_tip_db, logos::mdb_val(sizeof(key),const_cast<uint8_t*>(&key)), hash, transaction);
}

bool logos::block_store::micro_block_tip_get(block_hash &hash)
{
  const uint8_t i = 0; // only one tip
  mdb_val key(sizeof(i), const_cast<uint8_t*>(&i));
  return get<block_hash>(micro_block_tip_db, key, hash);
}

bool logos::block_store::account_get(logos::account const & account_a, logos::account_info & info_a)
{
    logos::mdb_val value;
    logos::transaction transaction(environment, nullptr, false);

    auto status (mdb_get (transaction, account_db, logos::mdb_val (account_a), value));
    assert (status == 0 || status == MDB_NOTFOUND);
    bool result;
    if (status == MDB_NOTFOUND)
    {
        result = true;
    }
    else
    {
        logos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
        result = info_a.deserialize (stream);
        assert (!result);
    }
    return result;
}

bool logos::block_store::account_db_empty()
{
    logos::transaction transaction(environment, nullptr, false);

    logos::store_iterator begin(transaction, account_db);
    logos::store_iterator end(nullptr);

    return begin == end;
}

void logos::block_store::account_put(const logos::account & account, const logos::account_info & info, MDB_txn * transaction)
{
    auto status(mdb_put(transaction, account_db, logos::mdb_val(account), info.val(), 0));

    assert(status == 0);
}

void logos::block_store::receive_put(const block_hash & hash, const state_block & block, MDB_txn * transaction)
{
    auto status(mdb_put(transaction, receive_db, logos::mdb_val(hash),
                        mdb_val(sizeof(state_block),
                                const_cast<state_block *>(&block)), 0));

    assert(status == 0);
}

bool logos::block_store::receive_exists(const block_hash & hash)
{
    logos::mdb_val junk;
    logos::transaction transaction(environment, nullptr, false);

    auto status(mdb_get(transaction, receive_db, logos::mdb_val(hash), junk));
    assert(status == 0 || status == MDB_NOTFOUND);

    return status == 0;
}

void logos::block_store::batch_tip_put(uint8_t delegate_id, const block_hash & hash, MDB_txn * transaction)
{
    auto status(mdb_put(transaction, batch_tips_db, logos::mdb_val(sizeof(delegate_id),
                                                                 &delegate_id),
                        mdb_val(sizeof(block_hash),
                                const_cast<block_hash *>(&hash)), 0));

    assert(status == 0);
}

bool logos::block_store::batch_tip_get(uint8_t delegate_id, block_hash & hash)
{
    logos::mdb_val value;
    logos::transaction transaction(environment, nullptr, false);

    auto status (mdb_get (transaction, batch_tips_db, logos::mdb_val(sizeof(delegate_id),
                                                                          &delegate_id), value));
    assert (status == 0 || status == MDB_NOTFOUND);
    bool result;
    if (status == MDB_NOTFOUND)
    {
        result = true;
    }
    else
    {
        memcpy(&hash, reinterpret_cast<uint8_t const *> (value.data ()), value.size());
    }
    return result;
}

void logos::block_store::flush (MDB_txn * transaction_a)
{
    std::unordered_map<logos::account, std::shared_ptr<logos::vote>> sequence_cache_l;
    std::unordered_multimap<logos::block_hash, std::shared_ptr<logos::block>> unchecked_cache_l;
    {
        std::lock_guard<std::mutex> lock (cache_mutex);
        sequence_cache_l.swap (vote_cache);
        unchecked_cache_l.swap (unchecked_cache);
    }
    for (auto & i : unchecked_cache_l)
    {
        std::vector<uint8_t> vector;
        {
            logos::vectorstream stream (vector);
            logos::serialize_block (stream, *i.second);
        }
        auto status (mdb_put (transaction_a, unchecked, logos::mdb_val (i.first), logos::mdb_val (vector.size (), vector.data ()), 0));
        assert (status == 0);
    }
    for (auto i (sequence_cache_l.begin ()), n (sequence_cache_l.end ()); i != n; ++i)
    {
        std::vector<uint8_t> vector;
        {
            logos::vectorstream stream (vector);
            i->second->serialize (stream);
        }
        auto status1 (mdb_put (transaction_a, vote, logos::mdb_val (i->first), logos::mdb_val (vector.size (), vector.data ()), 0));
        assert (status1 == 0);
    }
}
std::shared_ptr<logos::vote> logos::block_store::vote_current (MDB_txn * transaction_a, logos::account const & account_a)
{
    assert (!cache_mutex.try_lock ());
    std::shared_ptr<logos::vote> result;
    auto existing (vote_cache.find (account_a));
    if (existing != vote_cache.end ())
    {
        result = existing->second;
    }
    else
    {
        result = vote_get (transaction_a, account_a);
    }
    return result;
}

std::shared_ptr<logos::vote> logos::block_store::vote_generate (MDB_txn * transaction_a, logos::account const & account_a, logos::raw_key const & key_a, std::shared_ptr<logos::block> block_a)
{
    std::lock_guard<std::mutex> lock (cache_mutex);
    auto result (vote_current (transaction_a, account_a));
    uint64_t sequence ((result ? result->sequence : 0) + 1);
    result = std::make_shared<logos::vote> (account_a, key_a, sequence, block_a);
    vote_cache[account_a] = result;
    return result;
}

std::shared_ptr<logos::vote> logos::block_store::vote_max (MDB_txn * transaction_a, std::shared_ptr<logos::vote> vote_a)
{
    std::lock_guard<std::mutex> lock (cache_mutex);
    auto current (vote_current (transaction_a, vote_a->account));
    auto result (vote_a);
    if (current != nullptr)
    {
        if (current->sequence > result->sequence)
        {
            result = current;
        }
    }
    vote_cache[vote_a->account] = result;
    return result;
}

logos::store_iterator logos::block_store::latest_begin (MDB_txn * transaction_a, logos::account const & account_a)
{
    logos::store_iterator result (transaction_a, accounts, logos::mdb_val (account_a));
    return result;
}

logos::store_iterator logos::block_store::latest_begin (MDB_txn * transaction_a)
{
    logos::store_iterator result (transaction_a, accounts);
    return result;
}

logos::store_iterator logos::block_store::latest_end ()
{
    logos::store_iterator result (nullptr);
    return result;
}