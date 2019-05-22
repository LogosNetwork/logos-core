#include <queue>
#include <logos/blockstore.hpp>
#include <logos/versioning.hpp>
#include <logos/lib/trace.hpp>
#include <logos/consensus/messages/util.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
#include <logos/rewards/epoch_rewards.hpp>
#include <logos/rewards/epoch_rewards_manager.hpp>
#include <logos/staking/voting_power.hpp>
#include <logos/staking/voting_power_manager.hpp>
#include <logos/staking/staking_manager.hpp>
#include <logos/staking/staked_funds.hpp>
#include <logos/staking/thawing_funds.hpp>

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
bool logos::block_store::put(MDB_dbi &db, const mdb_val &key, const T &t, MDB_txn *tx)
{
    std::vector<uint8_t> buf;
    auto status(mdb_put(tx, db, key, t.to_mdb_val(buf), 0));
    assert(status == 0);
    return status;
}

template<typename T>
logos::block_hash logos::block_store::put(MDB_dbi &db, const T &t, MDB_txn *transaction)
{
    auto key = t.Hash();
    put<T>(db, key, t, transaction);
    return key;
}

template<typename T>
bool logos::block_store::get(MDB_dbi &db, const mdb_val &key, T &t, MDB_txn *tx)
{
    logos::mdb_val value;

    int status = 0;
    if (tx == 0) {
        logos::transaction transaction(environment, nullptr, false);
        status = mdb_get(transaction, db, key, value);
    } else {
        status = mdb_get(tx, db, key, value);
    }
    assert (status == 0 || status == MDB_NOTFOUND);
    bool result;
    if (status == MDB_NOTFOUND)
    {
        result = true;
    }
    else
    {
        logos::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
        result = t.Deserialize (stream);
        assert (!result);
    }
    return result;
}

//explicit instantiation of template functions
template bool logos::block_store::get(MDB_dbi&, logos::mdb_val const &, EpochRewardsInfo &, MDB_txn*);
template bool logos::block_store::put(MDB_dbi&, logos::mdb_val const &, EpochRewardsInfo const &, MDB_txn*);
template bool logos::block_store::get(MDB_dbi&, logos::mdb_val const &, GlobalEpochRewardsInfo &, MDB_txn*);
template bool logos::block_store::put(MDB_dbi&, logos::mdb_val const &, GlobalEpochRewardsInfo const &, MDB_txn*);
template bool logos::block_store::get(MDB_dbi&, logos::mdb_val const &, VotingPowerInfo&, MDB_txn*);
template bool logos::block_store::put(MDB_dbi&, logos::mdb_val const &, VotingPowerInfo const &, MDB_txn*);
template bool logos::block_store::get(MDB_dbi&, logos::mdb_val const &, StakedFunds&, MDB_txn*);
template bool logos::block_store::put(MDB_dbi&, logos::mdb_val const &, StakedFunds const &, MDB_txn*);
template bool logos::block_store::get(MDB_dbi&, logos::mdb_val const &, ThawingFunds&, MDB_txn*);
template bool logos::block_store::put(MDB_dbi&, logos::mdb_val const &, ThawingFunds const &, MDB_txn*);
template bool logos::block_store::get(MDB_dbi&, logos::mdb_val const &, Liability&, MDB_txn*);
template bool logos::block_store::put(MDB_dbi&, logos::mdb_val const &, Liability const &, MDB_txn*);
template bool logos::block_store::get(MDB_dbi&, logos::mdb_val const &, VotingPowerFallback&, MDB_txn*);
template bool logos::block_store::put(MDB_dbi&, logos::mdb_val const &, VotingPowerFallback const &, MDB_txn*);


bool logos::block_store::del(MDB_dbi &db, const mdb_val &key, MDB_txn *tx)
{
    auto status (mdb_del (tx, db, key, nullptr));

    auto error = status != 0;

    return error;
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
        error_a |= mdb_dbi_open (transaction, "reservation_db", MDB_CREATE, &reservation_db) != 0;
        error_a |= mdb_dbi_open (transaction, "receive_db", MDB_CREATE, &receive_db) != 0;
        error_a |= mdb_dbi_open (transaction, "request_tips_db", MDB_CREATE, &request_tips_db) != 0;

        // microblock-prototype
        error_a |= mdb_dbi_open (transaction, "micro_block_db", MDB_CREATE, &micro_block_db) != 0;
        error_a |= mdb_dbi_open (transaction, "micro_block_tip_db", MDB_CREATE, &micro_block_tip_db) != 0;

        // microblock-prototype
        error_a |= mdb_dbi_open (transaction, "epoch_db", MDB_CREATE, &epoch_db) != 0;
        error_a |= mdb_dbi_open (transaction, "epoch_tip_db", MDB_CREATE, &epoch_tip_db) != 0;

        // token platform
        error_a |= mdb_dbi_open (transaction, "token_user_status_db", MDB_CREATE, &token_user_status_db) != 0;

        // legacy
        error_a |= mdb_dbi_open (transaction, "frontiers", MDB_CREATE, &frontiers) != 0;
        error_a |= mdb_dbi_open (transaction, "state", MDB_CREATE, &state_blocks) != 0;
        error_a |= mdb_dbi_open (transaction, "pending", MDB_CREATE, &pending) != 0;
        error_a |= mdb_dbi_open (transaction, "blocks_info", MDB_CREATE, &blocks_info) != 0;
        error_a |= mdb_dbi_open (transaction, "representation", MDB_CREATE, &representation) != 0;
        error_a |= mdb_dbi_open (transaction, "unchecked", MDB_CREATE | MDB_DUPSORT, &unchecked) != 0;
        error_a |= mdb_dbi_open (transaction, "checksum", MDB_CREATE, &checksum) != 0;
        error_a |= mdb_dbi_open (transaction, "vote", MDB_CREATE, &vote) != 0;
        error_a |= mdb_dbi_open (transaction, "meta", MDB_CREATE, &meta) != 0;
        error_a |= mdb_dbi_open (transaction, "p2p_db", MDB_CREATE, &p2p_db) != 0;

        // elections
        error_a |= mdb_dbi_open (transaction, "representative_db", MDB_CREATE, &representative_db) != 0;
        error_a |= mdb_dbi_open (transaction, "candidacy_db", MDB_CREATE, &candidacy_db) != 0;
        error_a |= mdb_dbi_open (transaction, "leading_candidacy_db", MDB_CREATE, &leading_candidates_db);
        //Note, these databases use duplicate keys. The MDB_DUPSORT flag is necessary
        error_a |= mdb_dbi_open (transaction, "remove_candidates_db", MDB_CREATE | MDB_DUPSORT, &remove_candidates_db) != 0;
        error_a |= mdb_dbi_open (transaction, "remove_reps_db", MDB_CREATE | MDB_DUPSORT, &remove_reps_db);

        sync_leading_candidates(transaction);

        // address advertisement
        error_a |= mdb_dbi_open (transaction, "address_ad_db", MDB_CREATE, &address_ad_db) != 0;
        error_a |= mdb_dbi_open (transaction, "address_ad_tx_db", MDB_CREATE | MDB_DUPSORT, &address_ad_txa_db) != 0;
        //staking
        error_a |= mdb_dbi_open (transaction, "voting_power_db", MDB_CREATE, &voting_power_db);
        error_a |= mdb_dbi_open (transaction, "voting_power_fallback_db", MDB_CREATE, &voting_power_fallback_db);
        VotingPowerManager::SetInstance(*this);
        error_a |= mdb_dbi_open (transaction, "staking_db", MDB_CREATE, &staking_db);
        error_a |= mdb_dbi_open (transaction, "thawing_db", MDB_CREATE | MDB_DUPSORT, &thawing_db);
        StakingManager::SetInstance(*this);

        //liabilities
        error_a |= mdb_dbi_open (transaction, "master_liabilities_db", MDB_CREATE, &master_liabilities_db);
        error_a |= mdb_dbi_open (transaction, "rep_liabilities_db", MDB_CREATE | MDB_DUPSORT, &rep_liabilities_db);
        error_a |= mdb_dbi_open (transaction, "secondary_liabilities_db", MDB_CREATE | MDB_DUPSORT, &secondary_liabilities_db);

        //rewards
        error_a |= mdb_dbi_open (transaction, "epoch_rewards_db", MDB_CREATE, &epoch_rewards_db);
        error_a |= mdb_dbi_open (transaction, "global_epoch_rewards_db", MDB_CREATE, &global_epoch_rewards_db);
        EpochRewardsManager::SetInstance(*this);


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

void logos::block_store::clear (MDB_dbi db_a, MDB_txn * txn)
{
    int status = 0;
    if(txn == 0)
    {
        logos::transaction transaction (environment, nullptr, true);
        status  = mdb_drop (transaction, db_a, 0);
    }
    else
    {
        status = mdb_drop(txn, db_a, 0);
    }
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
    del(account_db, account_a, transaction_a);
}

bool logos::block_store::account_exists (MDB_txn * transaction_a, logos::account const & account_a)
{
    auto iterator (latest_begin (transaction_a, account_a));
    return iterator != logos::store_iterator (nullptr) && logos::account (iterator->first.uint256 ()) == account_a;
}

bool logos::block_store::account_get (MDB_txn * transaction_a, logos::account const & account_a, logos::account_info & info_a)
{
    return account_get(transaction_a, account_a, info_a, account_db);  // remove legacy "accounts" dbi
}

bool logos::block_store::account_get (MDB_txn * transaction_a, logos::account const & account_a, logos::account_info & info_a, MDB_dbi db)
{
    return get(account_db, account_a, info_a, transaction_a);
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
    auto status (mdb_stat (transaction_a, account_db, &frontier_stats));
    assert (status == 0);
    auto result (frontier_stats.ms_entries);
    return result;
}

void logos::block_store::account_put (MDB_txn * transaction_a, logos::account const & account_a, logos::account_info const & info_a)
{
    put(account_db, account_a, info_a, transaction_a);
}

void logos::block_store::reservation_put (AccountAddress const & account_a,
                                          logos::reservation_info const & info_a,
                                          MDB_txn * transaction_a)
{
    put(reservation_db, account_a, info_a, transaction_a);
}

bool logos::block_store::reservation_get (AccountAddress const & account_a,
                                          logos::reservation_info & info_a,
                                          MDB_txn * transaction_a)
{
    return get(reservation_db, account_a, info_a, transaction_a);
}

void logos::block_store::reservation_del (AccountAddress const & account_a, MDB_txn * transaction_a)
{
    del(reservation_db, account_a, transaction_a);
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


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

bool logos::block_store::consensus_block_get (const BlockHash& hash, ApprovedRB & block)
{
    return request_block_get(hash, block);
}
bool logos::block_store::consensus_block_get (const BlockHash& hash, ApprovedMB & block)
{
    return micro_block_get (hash, block);
}
bool logos::block_store::consensus_block_get (const BlockHash& hash, ApprovedEB & block)
{
    return epoch_get (hash, block);
}

bool logos::block_store::request_block_put(ApprovedRB const & block, MDB_txn * transaction)
{
    return request_block_put(block, block.Hash(), transaction);
}

bool logos::block_store::request_block_put(ApprovedRB const &block, const BlockHash &hash, MDB_txn *transaction)
{
    LOG_TRACE(log) << __func__ << " key " << hash.to_string();

    std::vector<uint8_t> buf;
    auto value(block.to_mdb_val(buf));
    auto status(mdb_put(transaction, batch_db, logos::mdb_val(hash),
                        value, 0));
    assert(status == 0);

    for(uint16_t i = 0; i < block.requests.size(); ++i)
    {
        status = request_put(*block.requests[i], transaction);
        assert(status == 0);
    }

    assert(status == 0);
    return status != 0;
}

bool logos::block_store::request_get(const BlockHash & hash, std::shared_ptr<Request> & request, MDB_txn * transaction)
{
    LOG_TRACE(log) << __func__ << " key " << hash.to_string();

    mdb_val val;
    if(mdb_get(transaction, state_db, mdb_val(hash), val))
    {
        LOG_TRACE(log) << __func__ << " mdb_get failed";
        return true;
    }

    bool error = false;
    request = DeserializeRequest(error, val);
    assert(!error);

    return error;
}

bool logos::block_store::request_put(const Request & request, MDB_txn * transaction)
{
    auto hash(request.GetHash());
    LOG_TRACE(log) << __func__ << " key " << hash.to_string();

    std::vector<uint8_t> buf;
    auto status(mdb_put(transaction, state_db, logos::mdb_val(request.GetHash()),
                        request.ToDatabase(buf), 0));

    assert(status == 0);
    return status != 0;
}

bool logos::block_store::request_exists(const Request & request)
{
    return request_exists(request.GetHash());
}

bool logos::block_store::request_exists(const BlockHash & hash)
{
    LOG_TRACE(log) << __func__ << " key " << hash.to_string();

    logos::mdb_val junk;
    logos::transaction transaction(environment, nullptr, false);

    auto status (mdb_get (transaction, state_db, logos::mdb_val (hash), junk));
    assert (status == 0 || status == MDB_NOTFOUND);

    return status == 0;
}

bool logos::block_store::request_block_get(const BlockHash & hash, ApprovedRB & block)
{
    transaction transaction(environment, nullptr, false);
    return request_block_get(hash, block, transaction);
}

bool logos::block_store::request_block_get(const BlockHash &hash, ApprovedRB &block, MDB_txn *transaction)
{
    LOG_TRACE(log) << __func__ << " key " << hash.to_string();

    mdb_val value;
    mdb_val key(hash);

    auto status (mdb_get (transaction, batch_db, key, value));
    assert (status == 0 || status == MDB_NOTFOUND);

    bool error = false;
    if (status == MDB_NOTFOUND)
    {
        LOG_TRACE(log) << __func__ << " MDB_NOTFOUND";
        error = true;
    }
    else
    {
        new(&block) ApprovedRB(error, value);
        assert(!error);

        if(!error)
        {
            if(block.hashes.size() > CONSENSUS_BATCH_SIZE)
            {
                LOG_FATAL(log) << __func__
                               << " request_block_get failed, block.request_count > CONSENSUS_BATCH_SIZE";
                trace_and_halt();
            }

            block.requests.reserve(block.hashes.size());
            for(uint16_t i = 0; i < block.hashes.size(); ++i)
            {
                block.requests.push_back(std::shared_ptr<Request>(nullptr));
                if(request_get(block.hashes[i], block.requests[i], transaction))
                {
                    LOG_ERROR(log) << __func__ << " request_get failed";
                    return true;
                }
            }
        }
    }

    return error;
}

bool logos::block_store::request_block_exists (const ApprovedRB & block)
{
    auto exists (true);
    logos::mdb_val junk;
    transaction transaction_a(environment, nullptr, false);

    auto status (mdb_get (transaction_a, batch_db, logos::mdb_val (block.Hash()), junk));
    assert (status == 0 || status == MDB_NOTFOUND);
    exists = status == 0;

    return exists;
}

void
logos::block_store::BatchBlocksIterator(
        const BatchTipHashes &start,
        const BatchTipHashes &end,
        IteratorBatchBlockReceiverCb batchblock_receiver)
{
    for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate)
    {
        BlockHash hash = start[delegate];
        ApprovedRB batch;
        bool not_found;
        for (not_found = request_block_get(hash, batch);
             !not_found && hash != end[delegate];
             hash = batch.previous, not_found = request_block_get(hash, batch))
        {
            batchblock_receiver(delegate, batch);
        }
        if (not_found && !hash.is_zero())
        {
            LOG_ERROR(log) << __func__ << " failed to get batch state block: "
                           << hash.to_string();
            return;
        }
    }
}

void
logos::block_store::BatchBlocksIterator(
        const BatchTipHashes &start,
        const uint64_t &cutoff,
        IteratorBatchBlockReceiverCb batchblock_receiver)
{
    for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate)
    {
        BlockHash hash = start[delegate];
        ApprovedRB batch;
        bool not_found = false;
        for (not_found = request_block_get(hash, batch);
             !not_found && batch.timestamp < cutoff;
             hash = batch.next, not_found = request_block_get(hash, batch))
        {
            batchblock_receiver(delegate, batch);
        }
        if (not_found && !hash.is_zero())
        {
            LOG_ERROR(log) << __func__ << " failed to get batch state block: "
                           << hash.to_string();
            return;
        }
    }
}

bool logos::block_store::consensus_block_update_next(const BlockHash & hash, const BlockHash & next, ConsensusType type, MDB_txn * transaction)
{
    LOG_TRACE(log) << __func__ << " key " << hash.to_string();

    mdb_val value;
    mdb_val key(hash);
    MDB_dbi db = 0; //typedef unsigned int    MDB_dbi, maybe use a naked pointer?

    switch(type){
    case ConsensusType::Request:
        db = batch_db;
        break;
    case ConsensusType::MicroBlock:
        db = micro_block_db;
        break;
    case ConsensusType::Epoch:
        db = epoch_db;
        break;
    default:
        LOG_FATAL(log) << __func__ << " wrong consensus type " << (uint)type;
        trace_and_halt();
    }

    auto status(mdb_get (transaction, db, key, value));
    if (status == MDB_NOTFOUND)
    {
        LOG_TRACE(log) << __func__ << " MDB_NOTFOUND";
        return true;
    }
    else if(status != 0)
    {
        LOG_FATAL(log) << __func__ << " failed to get consensus block "
                << ConsensusToName(type);
        trace_and_halt();
    }

    // From LMDB:
    //    The memory pointed to by the returned values is owned by the database.
    //    The caller need not dispose of the memory, and may not modify it in any
    //    way. For values returned in a read-only transaction any modification
    //    attempts will cause a SIGSEGV.
    //    Values returned from the database are valid only until a subsequent
    //    update operation, or the end of the transaction.
    auto data_size(value.size());
    std::vector<uint8_t> buf(data_size);
    mdb_val value_buf(data_size, buf.data());
    UpdateNext(value, value_buf, next);
    status = mdb_put(transaction, db, key, value_buf, 0);
    if(status != 0)
    {
        LOG_FATAL(log) << __func__ << " failed to put consensus block "
                       << ConsensusToName(type);
        trace_and_halt();
    }
    return false;
}

bool logos::block_store::get(MDB_dbi &db, const mdb_val &key, mdb_val &value, MDB_txn *tx)
{
    int status = 0;
    if (tx == 0)
    {
        logos::transaction transaction(environment, nullptr, false);
        status = mdb_get(transaction, db, key, value);
    }
    else
    {
        status = mdb_get(tx, db, key, value);
    }
    if( ! (status == 0 || status == MDB_NOTFOUND))
    {
        trace_and_halt();
    }

    bool error = (status == MDB_NOTFOUND);
    return error;
}

bool logos::block_store::micro_block_put(ApprovedMB const &block, MDB_txn *transaction)
{
    auto hash(block.Hash());
    LOG_TRACE(log) << __func__ << " key " << hash.to_string();

    std::vector<uint8_t> buf;
    auto status(mdb_put(transaction, micro_block_db, mdb_val(hash), block.to_mdb_val(buf), 0));
    assert(status == 0);
    return status != 0;
}

bool logos::block_store::micro_block_get(const BlockHash &hash, ApprovedMB &block, MDB_txn *transaction)
{
    LOG_TRACE(log) << __func__ << " key " << hash.to_string();

    mdb_val val;
    if(get(micro_block_db, mdb_val(hash), val, transaction))
    {
        return true;
    }

    bool error = false;
    new(&block) ApprovedMB(error, val);
    assert(!error);
    return error;
}

bool logos::block_store::micro_block_tip_put(const Tip & tip, MDB_txn *transaction)
{
    LOG_TRACE(log) << __func__ << " tip " << tip.to_string();

    const uint8_t key = 0; // only one tip
    std::vector<uint8_t> buf;
    auto status(mdb_put(transaction, micro_block_tip_db, logos::mdb_val(key), tip.to_mdb_val(buf), 0));
    assert(status == 0);
    return status != 0;
}

bool logos::block_store::micro_block_tip_get(Tip &tip, MDB_txn* t)
{
    const uint8_t key = 0; // only one tip
    mdb_val val;
    if(get(micro_block_tip_db, mdb_val(key), val, t))
    {
        return true;
    }
    assert(val.size() == Tip::WireSize);
    bool error = false;
    new (&tip) Tip(error, val);
    if(!error)
        LOG_TRACE(log) << __func__ << " tip " << tip.to_string();
    return error;
}

bool logos::block_store::micro_block_exists(const BlockHash &hash, MDB_txn *transaction)
{
    ApprovedMB mb;
    return (false == micro_block_get(hash, mb, transaction));
}

bool logos::block_store::micro_block_exists (const ApprovedMB & block)
{
    auto exists (true);
    logos::mdb_val junk;
    transaction transaction_a(environment, nullptr, false);

    auto status (mdb_get (transaction_a, micro_block_db, logos::mdb_val (block.Hash()), junk));
    assert (status == 0 || status == MDB_NOTFOUND);
    exists = status == 0;

    return exists;
}

bool logos::block_store::epoch_put(ApprovedEB const &block, MDB_txn *transaction)
{
    auto hash(block.Hash());
    LOG_TRACE(log) << "epoch_block_put key " << hash.to_string();

    std::vector<uint8_t> buf;
    auto status(mdb_put(transaction, epoch_db, mdb_val(hash), block.to_mdb_val(buf), 0));

    assert(status == 0);
    return status != 0;
}

bool logos::block_store::epoch_get(const BlockHash &hash, ApprovedEB &block, MDB_txn *transaction)
{
    LOG_TRACE(log) << "epoch_block_get key " << hash.to_string();

    mdb_val val;
    if(get(epoch_db, mdb_val(hash), val, transaction))
    {
        return true;
    }

    bool error = false;
    new(&block) ApprovedEB(error, val);
    assert(!error);
    return error;
}

bool logos::block_store::epoch_get_n(uint32_t num_epochs_ago, ApprovedEB &block, MDB_txn *txn, const std::function<bool(ApprovedEB&)>& filter)
{
    Tip tip;
    if(epoch_tip_get(tip, txn))
    {
        trace_and_halt();
    }
    BlockHash hash = tip.digest;
    for(size_t i = 0; i <= num_epochs_ago;)
    {
        assert(hash != 0);
        if (epoch_get(hash,block,txn))
        {
            trace_and_halt();
        }
        if(filter(block))
        {
            ++i;
        }
        hash = block.previous;
    }

    return false;
}

bool logos::block_store::epoch_tip_put(const Tip &tip, MDB_txn *transaction)
{
    LOG_TRACE(log) << __func__ << " tip " << tip.to_string();

    const uint8_t key = 0; // only one tip
    std::vector<uint8_t> buf;
    auto status(mdb_put(transaction, epoch_tip_db, logos::mdb_val(key), tip.to_mdb_val(buf), 0));
    assert(status == 0);
    return status != 0;
}

bool logos::block_store::epoch_tip_get(Tip &tip, MDB_txn *t)
{
    const uint8_t key = 0; // only one tip
    mdb_val val;
    if(get(epoch_tip_db, mdb_val(key), val, t))
    {
        return true;
    }
    assert(val.size() == Tip::WireSize);
    bool error = false;
    new (&tip) Tip(error, val);
    if(!error)
        LOG_TRACE(log) << __func__ << " tip " << tip.to_string();

    return error;
}

bool logos::block_store::epoch_exists (const ApprovedEB & block)
{
    auto exists(true);
    logos::mdb_val junk;
    transaction transaction_a(environment, nullptr, false);

    auto status(mdb_get(transaction_a, epoch_db, logos::mdb_val(block.Hash()), junk));
    assert (status == 0 || status == MDB_NOTFOUND);
    exists = status == 0;

    return exists;
}

bool logos::block_store::epoch_exists (const BlockHash &hash, MDB_txn *transaction)
{
    ApprovedEB eb;
    return (false == epoch_get(hash, eb, transaction));
}

bool logos::block_store::rep_get(AccountAddress const & account, RepInfo & rep_info, MDB_txn* transaction)
{
    LOG_TRACE(log) << __func__ << " key " << account.to_string();
    mdb_val val;
    if(get(representative_db, mdb_val(account), val, transaction))
    {
        return true;
    }

    bool error = false;
    new (&rep_info) RepInfo(error, val);
    assert (!error);
    return error;
}

bool logos::block_store::rep_put(
        const AccountAddress & account, 
        const RepInfo & rep_info,
        MDB_txn * transaction)
{
    std::vector<uint8_t> buf;
    auto status(mdb_put(transaction, representative_db, logos::mdb_val(account), rep_info.to_mdb_val(buf), 0));

    assert(status == 0);
    return status != 0;
}

bool logos::block_store::candidate_get(AccountAddress const & account, CandidateInfo & candidate_info, MDB_txn* transaction)
{
    LOG_TRACE(log) << __func__ << " key " << account.to_string();
    mdb_val val;
    if(get(candidacy_db, mdb_val(account), val, transaction))
    {
        return true;
    }

    bool error = false;
    new (&candidate_info) CandidateInfo(error, val);
    assert (!error);
    return error;
}

bool logos::block_store::candidate_put(
        const AccountAddress & account, 
        const CandidateInfo & candidate_info,
        MDB_txn * transaction)
{
    std::vector<uint8_t> buf;
    auto status(mdb_put(transaction, candidacy_db, logos::mdb_val(account), candidate_info.to_mdb_val(buf), 0));

    
    assert(status == 0);
    return update_leading_candidates(account,candidate_info,transaction);
}

bool logos::block_store::candidate_is_greater(
        const AccountAddress& account1,
        const CandidateInfo& candidate1,
        const AccountAddress& account2,
        const CandidateInfo& candidate2)
{
    std::string key_str = "3059301306072a8648ce3d020106082a8648ce3d030107034200048e1ad7"
                          "98008baac3663c0c1a6ce04c7cb632eb504562de923845fccf39d1c46dee"
                          "52df70f6cf46f1351ce7ac8e92055e5f168f5aff24bcaab7513d447fd677d3";
    ECIESPublicKey pk(key_str, true);
    Delegate del1(
          account1,
          0,
          pk,
          candidate1.votes_received_weighted,
          candidate1.cur_stake); 
    Delegate del2(
          account2,
          0,
          pk,
          candidate2.votes_received_weighted,
          candidate2.cur_stake);

   return EpochVotingManager::IsGreater(del1,del2);
}

void logos::block_store::sync_leading_candidates(MDB_txn* txn)
{

    size_t num_leading = 0;
    std::pair<AccountAddress,CandidateInfo> min_candidate;
    for(auto it = logos::store_iterator(txn, leading_candidates_db);
            it != logos::store_iterator(nullptr); ++it)
    {

        bool error = false;
        CandidateInfo current_candidate(error, it->second);
        assert(!error);
        ++num_leading;
        if(num_leading == 1 || 
                !candidate_is_greater(it->first.uint256(), current_candidate, min_candidate.first, min_candidate.second))
        {
            min_candidate = 
                std::make_pair(it->first.uint256(),current_candidate);
        }
    }

    leading_candidates_size = num_leading;
    min_leading_candidate = min_candidate;
}

bool logos::block_store::update_leading_candidates(
        const AccountAddress & account,
        const CandidateInfo & candidate_info,
        MDB_txn* txn)
{
    bool leading_candidates_full = 
        leading_candidates_size == (NUM_DELEGATES / EpochVotingManager::TERM_LENGTH);

    //check if candidate is already in leading_candidates_db
    mdb_val val;
    if(!get(leading_candidates_db, mdb_val(account), val, txn))
    {
        std::vector<uint8_t> buf;
        auto status = mdb_put(txn,
                leading_candidates_db,
                logos::mdb_val(account),
                candidate_info.to_mdb_val(buf), 0);

        assert(status == 0);
        //min could be different if this candidate was min
        if(min_leading_candidate.first == account && leading_candidates_full)
        {
            sync_leading_candidates(txn);
        }
        return status != 0;
    }

    if(leading_candidates_full)
    {
        if(candidate_is_greater(account, candidate_info,
                    min_leading_candidate.first, min_leading_candidate.second))
        {
            auto status(mdb_del(txn,
                        leading_candidates_db,
                        logos::mdb_val(min_leading_candidate.first),
                        nullptr));

            assert(status == 0);
            std::vector<uint8_t> buf;
            status = mdb_put(txn,
                    leading_candidates_db,
                    logos::mdb_val(account),
                    candidate_info.to_mdb_val(buf),
                    0);

            assert(status == 0);
            sync_leading_candidates(txn);
            return status != 0;
        }
        return false;
    } else
    {
        std::vector<uint8_t> buf;
        auto status(mdb_put(txn,
                    leading_candidates_db,
                    logos::mdb_val(account),
                    candidate_info.to_mdb_val(buf),
                    0));

        assert(status == 0);
        leading_candidates_size++;
        if(leading_candidates_size == 
                (NUM_DELEGATES / EpochVotingManager::TERM_LENGTH))
        {
            sync_leading_candidates(txn);
        }
        return status != 0;
    }
}

bool logos::block_store::candidate_add_vote(
        const AccountAddress & account,
        Amount weighted_vote,
        uint32_t cur_epoch_num,
        MDB_txn * txn)
{
    CandidateInfo info;
    if(!candidate_get(account,info,txn))
    {
        info.TransitionIfNecessary(cur_epoch_num);
        info.votes_received_weighted += weighted_vote;

        return candidate_put(account,info,txn);
    }
    return true;
}

bool logos::block_store::candidate_mark_remove(
        const AccountAddress & account,
        MDB_txn * txn)
{
    const uint8_t key = 0; // only one key
    auto status(mdb_put(txn, remove_candidates_db, logos::mdb_val(key), logos::mdb_val(account), 0));
    assert(status == 0);

    return status != 0;
}

bool logos::block_store::rep_mark_remove(
        const AccountAddress & account,
        MDB_txn * txn)
{
    const uint8_t key = 0; // only one key
    auto status(mdb_put(txn, remove_reps_db, logos::mdb_val(key), logos::mdb_val(account), 0));
    assert(status == 0);

    return status != 0;
}

bool logos::block_store::token_user_status_get(const BlockHash & token_user_id, TokenUserStatus & status, MDB_txn * transaction)
{
    LOG_TRACE(log) << __func__ << " key " << token_user_id.to_string();

    mdb_val val;
    if(get(token_user_status_db, mdb_val(token_user_id), val, transaction))
    {
        return true;
    }

    bool error = false;
    new (&status) TokenUserStatus(error, val);

    if(error)
    {
        LOG_FATAL(log) << __func__ << " key " << token_user_id.to_string()
                       << " - failed to deserialize TokenUserStatus";

        trace_and_halt();
    }

    return false;
}

bool logos::block_store::token_user_status_put(const BlockHash & token_user_id, const TokenUserStatus & status, MDB_txn * transaction)
{
    std::vector<uint8_t> buf;
    auto result(mdb_put(transaction, token_user_status_db, logos::mdb_val(token_user_id), status.ToMdbVal(buf), 0));

    assert(result == 0);
    return result != 0;
}

bool logos::block_store::token_user_status_del(const BlockHash & token_user_id, MDB_txn * transaction)
{
    return del(token_user_status_db, token_user_id, transaction);
}

bool logos::block_store::token_account_get(const BlockHash & token_id, TokenAccount & info, MDB_txn* transaction)
{
    LOG_TRACE(log) << __func__ << " key " << token_id.to_string();
    mdb_val val;
    if(get(account_db, mdb_val(token_id), val, transaction))
    {
        return true;
    }

    bool error = false;
    new (&info) TokenAccount(error, val);

    return false;
}

bool logos::block_store::token_account_put(const BlockHash & token_id, const TokenAccount & info, MDB_txn * transaction)
{
    std::vector<uint8_t> buf;
    auto status(mdb_put(transaction, account_db, logos::mdb_val(token_id), info.to_mdb_val(buf), 0));

    assert(status == 0);
    return status != 0;
}

bool logos::block_store::account_get(AccountAddress const & account_a, std::shared_ptr<Account> & info_a, MDB_txn* transaction)
{
    LOG_TRACE(log) << __func__ << " key " << account_a.to_string();
    mdb_val val;

    if(get(account_db, mdb_val(account_a), val, transaction))
    {
        return true;
    }

    bool error = false;
    info_a = DeserializeAccount(error, val);

    assert (!error);
    return error;
}

bool logos::block_store::is_first_epoch()
{
    Tip epoch_tip;

    if (epoch_tip_get(epoch_tip))
    {
        LOG_ERROR(log) << __func__ << " failed to get epoch tip. Genesis blocks are being generated.";
        return true;
    }

    ApprovedEB epoch;
    if (epoch_get(epoch_tip.digest, epoch))
    {
        LOG_FATAL(log) << __func__ << " failed to get epoch.";
        trace_and_halt();
    }

    return epoch.epoch_number == GENESIS_EPOCH;
}

uint32_t logos::block_store::epoch_number_stored()
{
    Tip epoch_tip;
    if (epoch_tip_get(epoch_tip))
    {
        LOG_FATAL(log) << __func__ << " epoch tip doesn't exist.";
        trace_and_halt();
    }

    return epoch_tip.epoch;
}

void
logos::block_store::GetEpochFirstRBs(uint32_t epoch_number, BatchTips & epoch_firsts)
{
    BatchTipHashes start, end;

    // `start` is current epoch tip, `end` is empty
    for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate)
    {
        Tip tip;
        if (request_tip_get(delegate, epoch_number, tip))
        {
            LOG_DEBUG(log) << __func__ << " request block tip for delegate "
                            << std::to_string(delegate) << " for epoch number " << epoch_number
                            << " doesn't exist yet, setting to zero.";
        }else{
            start[delegate] = tip.digest;
        }
    }

    // iterate backwards from current tip till the gap (i.e. beginning of this current epoch)
    BatchBlocksIterator(start, end, [&](uint8_t delegate, const ApprovedRB &batch)mutable->void{
        if (batch.previous.is_zero())
        {
            epoch_firsts[delegate] = batch.CreateTip();
        }
    });
}

bool logos::block_store::account_get(AccountAddress const & account_a, account_info & info_a, MDB_txn* transaction)
{
    LOG_TRACE(log) << __func__ << " key " << account_a.to_string();
    mdb_val val;
    if(get(account_db, mdb_val(account_a), val, transaction))
    {
        return true;
    }

    bool error = false;
    new (&info_a) account_info(error, val);
    assert (!error);
    return error;
}

bool logos::block_store::account_db_empty()
{
    logos::transaction transaction(environment, nullptr, false);

    logos::store_iterator begin(transaction, account_db);
    logos::store_iterator end(nullptr);

    return begin == end;
}

bool logos::block_store::account_put(const AccountAddress & account, std::shared_ptr<Account> info, MDB_txn * transaction)
{
    return info->type == AccountType::LogosAccount ?
           account_put(account, *static_pointer_cast<account_info>(info), transaction) :
           token_account_put(account, *static_pointer_cast<TokenAccount>(info), transaction);
}

bool logos::block_store::account_put(const AccountAddress & account, const logos::account_info & info, MDB_txn * transaction)
{
    std::vector<uint8_t> buf;
    auto status(mdb_put(transaction, account_db, logos::mdb_val(account), info.to_mdb_val(buf), 0));

    assert(status == 0);
    return status != 0;
}

bool logos::block_store::account_exists(AccountAddress const & address)
{
    LOG_TRACE(log) << __func__ << " key " << address.to_string();

    logos::mdb_val junk;
    logos::transaction transaction(environment, nullptr, false);

    auto status (mdb_get (transaction, account_db, logos::mdb_val (address), junk));
    assert (status == 0 || status == MDB_NOTFOUND);

    return status == 0;
}

bool logos::block_store::receive_put(const BlockHash & hash, const ReceiveBlock & block, MDB_txn * transaction)
{
    LOG_TRACE(log) << __func__ << " key " << hash.to_string();

    std::vector<uint8_t> buf;
    auto status(mdb_put(transaction, receive_db, logos::mdb_val(hash),
            block.to_mdb_val(buf), 0));

    assert(status == 0);
    return status != 0;
}

bool logos::block_store::receive_get (const BlockHash & hash, ReceiveBlock & block, MDB_txn * transaction)
{
    LOG_TRACE(log) << __func__ << " key " << hash.to_string();

    logos::mdb_val value;

    auto status (mdb_get (transaction, receive_db, mdb_val(hash), value));
    assert (status == 0 || status == MDB_NOTFOUND);
    bool error = false;
    if (status == MDB_NOTFOUND)
    {
        error = true;
    }
    else
    {
        new (&block) ReceiveBlock(error, value);
    }
    return error;
}

bool logos::block_store::receive_exists(const BlockHash & hash)
{
    LOG_TRACE(log) << __func__ << " key " << hash.to_string();

    logos::mdb_val junk;
    logos::transaction transaction(environment, nullptr, false);

    auto status(mdb_get(transaction, receive_db, logos::mdb_val(hash), junk));
    assert(status == 0 || status == MDB_NOTFOUND);

    return status == 0;
}

bool logos::block_store::request_tip_put(uint8_t delegate_id, uint32_t epoch_number, const Tip & tip, MDB_txn * transaction)
{
    LOG_TRACE(log) << __func__  << " key " << (uint)delegate_id << ":" << epoch_number << " value " << tip.to_string();
    auto key(logos::get_request_tip_key(delegate_id, epoch_number));

    std::vector<uint8_t> buf;
    auto status(mdb_put(transaction,
            request_tips_db,
            mdb_val(key),
            tip.to_mdb_val(buf),
            0));

    assert(status == 0);
    return status != 0;
}

bool logos::block_store::request_tip_get(uint8_t delegate_id, uint32_t epoch_number, Tip & tip, MDB_txn *t)
{
    mdb_val val;
    auto key(logos::get_request_tip_key(delegate_id, epoch_number));
    if(get(request_tips_db, mdb_val(key), val, t))
    {
        LOG_TRACE(log) << __func__ << " cannot find " << (uint)delegate_id << ":" << epoch_number;
        return true;
    }
    assert(val.size() == Tip::WireSize);
    bool error = false;
    new (&tip) Tip(error, val);
    if(!error)
        LOG_TRACE(log) << __func__ << " tip " << tip.to_string();
    return error;
}

bool logos::block_store::request_tip_del(uint8_t delegate_id, uint32_t epoch_number, MDB_txn * transaction)
{
    LOG_TRACE(log) << __func__ << " delegate " << (int)delegate_id << ", epoch " << epoch_number;
    auto key = logos::get_request_tip_key(delegate_id, epoch_number);
    return del(request_tips_db, mdb_val(key), transaction);
}

// should only be used for the first request block of an epoch!
bool logos::block_store::request_block_update_prev(const BlockHash & hash, const BlockHash & prev, MDB_txn * transaction)
{
    LOG_TRACE(log) << __func__ << " key " << hash.to_string();

    mdb_val value;
    mdb_val key(hash);

    auto status(mdb_get (transaction, batch_db, key, value));
    if (status == MDB_NOTFOUND)
    {
        LOG_TRACE(log) << __func__ << " MDB_NOTFOUND";
        return true;
    }
    else if(status != 0)
    {
        LOG_FATAL(log) << __func__ << " failed to get consensus block "
                       << ConsensusToName(ConsensusType::Request);
        trace_and_halt();
    }

    auto data_size(value.size());
    std::vector<uint8_t> buf(data_size);
    mdb_val value_buf(data_size, buf.data());
    update_PostCommittedRequestBlock_prev_field(value, value_buf, prev);
    status = mdb_put(transaction, batch_db, key, value_buf, 0);
    if(status != 0)
    {
        LOG_FATAL(log) << __func__ << " failed to put consensus block "
                       << ConsensusToName(ConsensusType::Request);
        trace_and_halt();
    }
    return false;
}

// will not fix unless needed
//void logos::block_store::flush (MDB_txn * transaction_a)
//{
//    std::unordered_map<logos::account, std::shared_ptr<logos::vote>> sequence_cache_l;
//    std::unordered_multimap<logos::block_hash, std::shared_ptr<logos::block>> unchecked_cache_l;
//    {
//        std::lock_guard<std::mutex> lock (cache_mutex);
//        sequence_cache_l.swap (vote_cache);
//        unchecked_cache_l.swap (unchecked_cache);
//    }
//    for (auto & i : unchecked_cache_l)
//    {
//        std::vector<uint8_t> vector;
//        {
//            logos::vectorstream stream (vector);
//            logos::serialize_block (stream, *i.second);
//        }
//        auto status (mdb_put (transaction_a, unchecked, logos::mdb_val (i.first), logos::mdb_val (vector.size (), vector.data ()), 0));
//        assert (status == 0);
//    }
//    for (auto i (sequence_cache_l.begin ()), n (sequence_cache_l.end ()); i != n; ++i)
//    {
//        std::vector<uint8_t> vector;
//        {
//            logos::vectorstream stream (vector);
//            i->second->serialize (stream);
//        }
//        auto status1 (mdb_put (transaction_a, vote, logos::mdb_val (i->first), logos::mdb_val (vector.size (), vector.data ()), 0));
//        assert (status1 == 0);
//    }
//}
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
    logos::store_iterator result (transaction_a, account_db, logos::mdb_val (account_a));
    return result;
}

logos::store_iterator logos::block_store::latest_begin (MDB_txn * transaction_a)
{
    logos::store_iterator result (transaction_a, account_db);
    return result;
}

logos::store_iterator logos::block_store::latest_end ()
{
    logos::store_iterator result (nullptr);
    return result;
}

uint64_t logos::get_request_tip_key(uint8_t delegate_id, uint32_t epoch_number)
{
    uint64_t res = delegate_id;
    return (res << 32) | epoch_number;
}

uint32_t logos::block_store::consensus_block_get_raw(const BlockHash & hash,
        ConsensusType type,
        uint32_t reserve,
        std::vector<uint8_t> & buf)
{
    LOG_TRACE(log) << __func__ << " key " << hash.to_string();

    mdb_val value;
    mdb_val key(hash);
    MDB_dbi db = 0; //typedef unsigned int    MDB_dbi, maybe use a naked pointer?
    logos::transaction transaction(environment, nullptr, false);

    switch(type){
    case ConsensusType::Request:
        db = batch_db;
        break;
    case ConsensusType::MicroBlock:
        db = micro_block_db;
        break;
    case ConsensusType::Epoch:
        db = epoch_db;
        break;
    default:
        LOG_FATAL(log) << __func__ << " wrong consensus type " << (uint)type;
        trace_and_halt();
    }

    auto status(mdb_get (transaction, db, key, value));
    if (status == MDB_NOTFOUND)
    {
        LOG_TRACE(log) << __func__ << " MDB_NOTFOUND";
        return 0;
    }
    else if(status != 0)
    {
        LOG_FATAL(log) << __func__ << " error when getting a consensus block "
                << ConsensusToName(type);
        trace_and_halt();
    }

    uint32_t block_size = value.size();
    buf.resize(reserve + block_size);
    memcpy(buf.data()+reserve, value.data(), block_size);
    return block_size;
#if 0
    if(type == ConsensusType::MicroBlock || type == ConsensusType::Epoch)
    {
        uint32_t block_size = value.size();
        buf.resize(reserve + block_size);
        memcpy(buf.data()+reserve, value.data(), block_size);
        return block_size;
    }
    else
    {
        bool error = false;
        ApprovedRB block;
        new(&block) ApprovedRB(error, value);
        assert(!error);
//TODO
    }
#endif
}

template<typename KeyType, typename ... Args>
bool
logos::block_store::ad_get(MDB_txn *t, std::vector<uint8_t> &data, Args ... args)
{
    KeyType key{args ... };
    mdb_val value;

    auto db = get_ad_db<KeyType>();
    int status = 0;
    if (t == 0) {
        logos::transaction transaction(environment, nullptr, false);
        status = mdb_get(transaction, db, mdb_val(sizeof(key), &key), value);
    } else {
        status = mdb_get(t, db, mdb_val(sizeof(key), &key), value);
    }
    assert (status == 0 || status == MDB_NOTFOUND);
    bool result;
    if (status == MDB_NOTFOUND)
    {
        result = true;
    }
    else
    {
        data.resize(value.size());
        memcpy(data.data(), value.data(), value.size());
    }
    return result;
}

bool logos::block_store::stake_put(
        AccountAddress const & account,
        StakedFunds const & funds,
        MDB_txn* txn)
{
    auto error = put(staking_db, logos::mdb_val(account), funds, txn);
    if(error)
    {
        LOG_FATAL(log) << "block_store::stake_put - "
            << "error storing StakedFunds. account = "
            << account.to_string();
        trace_and_halt();
    }
    return error;
}

bool logos::block_store::stake_get(
        AccountAddress const & account,
        StakedFunds & funds,
        MDB_txn* txn)
{
    return get(staking_db, logos::mdb_val(account), funds, txn);
}

bool logos::block_store::stake_del(
        AccountAddress const & account,
        MDB_txn* txn)
{
    return del(staking_db, logos::mdb_val(account), txn);
}

bool logos::block_store::thawing_put(
        AccountAddress const & account,
        ThawingFunds const & funds,
        MDB_txn* txn)
{
    auto error = put(thawing_db, logos::mdb_val(account), funds, txn);
    if(error)
    {
        LOG_FATAL(log) << "block_store::thawing_put - "
            << "error storing StakedFunds. account = "
            << account.to_string();
        trace_and_halt();
    }
    return error;
}

bool logos::block_store::thawing_del(
        AccountAddress const & account,
        ThawingFunds const & funds,
        MDB_txn* txn)
{
    std::vector<uint8_t> buf;
    return mdb_del(txn, thawing_db, logos::mdb_val(account), funds.to_mdb_val(buf));
}

bool logos::block_store::liability_get(
        LiabilityHash const & hash,
        Liability & l,
        MDB_txn* txn)
{
    return get(master_liabilities_db, logos::mdb_val(hash), l, txn);
}

bool logos::block_store::liability_exists(
        LiabilityHash const & hash,
        MDB_txn* txn)
{
    Liability l;
    return !get(master_liabilities_db, logos::mdb_val(hash), l, txn);
}

bool logos::block_store::liability_put(
        Liability const & l,
        MDB_txn* txn)
{
    Liability existing;
    LiabilityHash hash = l.Hash();
    //if liability with same expiration, target and source exists, consolidate
    bool error = false;
    if(!get(master_liabilities_db, logos::mdb_val(hash), existing, txn))
    {
        existing.amount += l.amount;
        error = put(master_liabilities_db, logos::mdb_val(hash), existing, txn);
    }
    else
    {
        error = put(master_liabilities_db, logos::mdb_val(hash), l, txn);
        error |= mdb_put(txn, rep_liabilities_db, logos::mdb_val(l.target), logos::mdb_val(hash), 0);
    }
    if(error)
    {
        LOG_FATAL(log) << "block_store::liability_put - "
            << "error storing liability - "
            << "hash = " << l.Hash().to_string();
        trace_and_halt();
    }
    return error;
}

bool logos::block_store::liability_update_amount(
        LiabilityHash const & hash,
        Amount const & amount,
        MDB_txn* txn)
{
    Liability l;
    if(liability_get(hash, l, txn))
    {
        LOG_FATAL(log) << "LiabilityManager::UpdateLiabilityAmount - "
            << "liability does not exist for hash = " << hash.to_string();
        trace_and_halt();
    }
    l.amount = amount;
    auto error = put(master_liabilities_db, logos::mdb_val(hash), l, txn);
    if(error)
    {
        LOG_FATAL(log) << "block_store::liability_update_amount - "
            << "error storing liability - "
            << "hash = " << hash.to_string();
        trace_and_halt();
    }
    return error;
}

bool logos::block_store::secondary_liability_put(
        AccountAddress const & source,
        LiabilityHash const & hash,
        MDB_txn* txn)
{
    
    auto error = mdb_put(txn, secondary_liabilities_db, logos::mdb_val(source), logos::mdb_val(hash), 0);
    if(error)
    {
        LOG_FATAL(log) << "block_store::secondary_liability_put - "
            << "error storing liability hash - "
            << "hash = " << hash.to_string();
        trace_and_halt();
    }
    return error;
}

bool logos::block_store::liability_del(
        LiabilityHash const & hash,
        MDB_txn* txn)
{
    Liability l;
    if(get(master_liabilities_db, logos::mdb_val(hash), l, txn))
    {
        LOG_FATAL(log) << "block_store::liability_del - "
            << "liability does not exist for hash = " << hash.to_string();
        trace_and_halt();
    }
    auto error = mdb_del(txn, rep_liabilities_db, logos::mdb_val(l.target), logos::mdb_val(hash));
    error |= del(master_liabilities_db, logos::mdb_val(hash), txn);
    if(error)
    {
        LOG_FATAL(log) << "block_store::liability_del - "
            << "error deleting liability with hash = " << hash.to_string();
        trace_and_halt();
    }
    return error;
}

bool logos::block_store::secondary_liability_del(
        LiabilityHash const & hash,
        MDB_txn* txn)
{
    Liability l;
    if(get(master_liabilities_db, logos::mdb_val(hash), l, txn))
    {
        LOG_FATAL(log) << "block_store::secondary_liability_del - "
            << "liability does not exist for hash = " << hash.to_string();
        trace_and_halt();
    }
    auto error = mdb_del(txn, secondary_liabilities_db, logos::mdb_val(l.source), logos::mdb_val(hash));
    error |= del(master_liabilities_db, logos::mdb_val(hash), txn);
    if(error)
    {
        LOG_FATAL(log) << "block_store::liability_del - "
            << "error deleting liability with hash = " << hash.to_string();
        trace_and_halt();
    }
    return error;
}

bool logos::block_store::voting_power_get(
        AccountAddress const & rep,
        VotingPowerInfo & info,
        MDB_txn* txn)
{
    return get(voting_power_db, rep, info, txn);
}

bool logos::block_store::voting_power_put(
        AccountAddress const & rep,
        VotingPowerInfo const & info,
        MDB_txn* txn)
{
    bool error = put(voting_power_db, logos::mdb_val(rep), info, txn);
    if(error)
    {
        LOG_FATAL(log) << "block_store::voting_power_put - "
            << "error putting VotingPowerInfo with rep = "
            << rep.to_string();
        trace_and_halt();
    }
    return error;
}

bool logos::block_store::voting_power_del(
        AccountAddress const & rep,
        MDB_txn* txn)
{
    return del(voting_power_db, rep, txn);
}

bool logos::block_store::fallback_voting_power_get(
        AccountAddress const & rep,
        VotingPowerFallback & f,
        MDB_txn* txn)
{
    return get(voting_power_fallback_db, rep, f, txn);
}

bool logos::block_store::fallback_voting_power_put(
        AccountAddress const & rep,
        VotingPowerFallback const & f,
        MDB_txn* txn)
{
    bool error = put(voting_power_fallback_db, logos::mdb_val(rep), f, txn);
    if(error)
    {
        LOG_FATAL(log) << "block_store::fallback_voting_power_put - "
            << "error putting VotingPowerFallback with rep = "
            << rep.to_string();
        trace_and_halt();
    }
    return error;
}

bool logos::block_store::fallback_voting_power_del(
        AccountAddress const & rep,
        MDB_txn* txn)
{
    return del(voting_power_fallback_db, rep, txn);
}


