#include <logos/common.hpp>

#include <logos/blockstore.hpp>
#include <logos/lib/interface.h>
#include <logos/node/common.hpp>
#include <logos/versioning.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <queue>

#include <ed25519-donna/ed25519.h>

// Genesis keys for network variants
namespace
{
char const * test_private_key_data = "34F0A37AAD20F4A260F0A5B3CB3D7FB50673212263E58A380BC10474BB039CE4";
char const * test_public_key_data = "B0311EA55708D6A53C75CDBF88300259C6D018522FE3D4D0A242E431F9E8B6D0"; // lgs_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo
char const * beta_public_key_data = "0311B25E0D1E1D7724BBA5BD523954F1DBCFC01CB8671D55ED2D32C7549FB252"; // lgs_11rjpbh1t9ixgwkdqbfxcawobwgusz13sg595ocytdbkrxcbzekkcqkc3dn1
char const * live_public_key_data = "E89208DD038FBB269987689621D52292AE9C35941A7484756ECCED92A65093BA"; // lgs_3t6k35gi95xu6tergt6p69ck76ogmitsa8mnijtpxm9fkcm736xtoncuohr3
char const * test_genesis_data = R"%%%({
    "type": "open",
    "source": "B0311EA55708D6A53C75CDBF88300259C6D018522FE3D4D0A242E431F9E8B6D0",
    "representative": "lgs_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo",
    "account": "lgs_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo",
    "work": "9680625b39d3363d",
    "signature": "ECDA914373A2F0CA1296475BAEE40500A7F0A7AD72A5A80C81D7FAB7F6C802B2CC7DB50F5DD0FB25B2EF11761FA7344A158DD5A700B21BD47DE5BD0F63153A02"
})%%%";

char const * beta_genesis_data = R"%%%({
    "type": "open",
    "source": "0311B25E0D1E1D7724BBA5BD523954F1DBCFC01CB8671D55ED2D32C7549FB252",
    "representative": "lgs_11rjpbh1t9ixgwkdqbfxcawobwgusz13sg595ocytdbkrxcbzekkcqkc3dn1",
    "account": "lgs_11rjpbh1t9ixgwkdqbfxcawobwgusz13sg595ocytdbkrxcbzekkcqkc3dn1",
    "work": "869e17b2bfa36639",
    "signature": "34DF447C7F185673128C3516A657DFEC7906F16C68FB5A8879432E2E4FB908C8ED0DD24BBECFAB3C7852898231544A421DC8CB636EF66C82E1245083EB08EA0F"
})%%%";

char const * live_genesis_data = R"%%%({
    "type": "open",
    "source": "E89208DD038FBB269987689621D52292AE9C35941A7484756ECCED92A65093BA",
    "representative": "lgs_3t6k35gi95xu6tergt6p69ck76ogmitsa8mnijtpxm9fkcm736xtoncuohr3",
    "account": "lgs_3t6k35gi95xu6tergt6p69ck76ogmitsa8mnijtpxm9fkcm736xtoncuohr3",
    "work": "62f05417dd3fb691",
    "signature": "9F0C933C8ADE004D808EA1985FA746A7E95BA2A38F867640F53EC8F180BDFE9E2C1268DEAD7C2664F356E37ABA362BC58E46DBA03E523A7B5A19E4B6EB12BB02"
})%%%";

char const * logos_genesis_data = R"%%%({
    "type": "state",
    "account": "lgs_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo",
    "previous": "0",
    "representative": "lgs_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo",
    "amount": "340282366920938463463374607431768211455",
    "link": "B0311EA55708D6A53C75CDBF88300259C6D018522FE3D4D0A242E431F9E8B6D0",
    "work": "0",
    "signature": "ECDA914373A2F0CA1296475BAEE40500A7F0A7AD72A5A80C81D7FAB7F6C802B2CC7DB50F5DD0FB25B2EF11761FA7344A158DD5A700B21BD47DE5BD0F63153A02"
})%%%";

class ledger_constants
{
public:
    ledger_constants () :
    zero_key ("0"),
    test_genesis_key (test_private_key_data),
    logos_test_account (test_public_key_data),
    logos_beta_account (beta_public_key_data),
    logos_live_account (live_public_key_data),
    logos_beta_genesis (beta_genesis_data),
    logos_live_genesis (live_genesis_data),
    logos_test_genesis (logos_genesis_data),
    genesis_account (logos::logos_network ==logos::logos_networks::logos_test_network ? logos_test_account : logos::logos_network ==logos::logos_networks::logos_beta_network ? logos_beta_account : logos_live_account),
    genesis_block (logos::logos_network ==logos::logos_networks::logos_test_network ? logos_test_genesis : logos::logos_network ==logos::logos_networks::logos_beta_network ? logos_beta_genesis : logos_live_genesis),
    genesis_amount (std::numeric_limits<logos::uint128_t>::max ()),
    burn_account (0)
    {
        CryptoPP::AutoSeededRandomPool random_pool;
        // Randomly generating these mean no two nodes will ever have the same sentinel values which protects against some insecure algorithms
        random_pool.GenerateBlock (not_a_block.bytes.data (), not_a_block.bytes.size ());
        random_pool.GenerateBlock (not_an_account.bytes.data (), not_an_account.bytes.size ());
    }
    logos::keypair zero_key;
    logos::keypair test_genesis_key;
    logos::account logos_test_account;
    logos::account logos_beta_account;
    logos::account logos_live_account;
    std::string logos_beta_genesis;
    std::string logos_live_genesis;
    std::string logos_test_genesis;
    logos::account genesis_account;
    std::string genesis_block;
    logos::uint128_t genesis_amount;
    logos::block_hash not_a_block;
    logos::account not_an_account;
    logos::account burn_account;
};
ledger_constants globals;
}

size_t constexpr logos::state_block::size;

logos::keypair const & logos::zero_key (globals.zero_key);
logos::keypair const & logos::test_genesis_key (globals.test_genesis_key);
logos::account const & logos::logos_test_account (globals.logos_test_account);
logos::account const & logos::logos_beta_account (globals.logos_beta_account);
logos::account const & logos::logos_live_account (globals.logos_live_account);
std::string const & logos::logos_test_genesis (globals.logos_test_genesis);
std::string const & logos::logos_beta_genesis (globals.logos_beta_genesis);
std::string const & logos::logos_live_genesis (globals.logos_live_genesis);

logos::account const & logos::genesis_account (globals.genesis_account);
std::string const & logos::genesis_block (globals.genesis_block);
logos::uint128_t const & logos::genesis_amount (globals.genesis_amount);
logos::block_hash const & logos::not_a_block (globals.not_a_block);
logos::block_hash const & logos::not_an_account (globals.not_an_account);
logos::account const & logos::burn_account (globals.burn_account);
std::vector<logos::genesis_delegate> logos::genesis_delegates;

logos::votes::votes (std::shared_ptr<logos::block> block_a) :
id (block_a->root ())
{
    rep_votes.insert (std::make_pair (logos::not_an_account, block_a));
}

logos::tally_result logos::votes::vote (std::shared_ptr<logos::vote> vote_a)
{
    logos::tally_result result;
    auto existing (rep_votes.find (vote_a->account));
    if (existing == rep_votes.end ())
    {
        // Vote on this block hasn't been seen from rep before
        result = logos::tally_result::vote;
        rep_votes.insert (std::make_pair (vote_a->account, vote_a->block));
    }
    else
    {
        if (!(*existing->second == *vote_a->block))
        {
            // Rep changed their vote
            result = logos::tally_result::changed;
            existing->second = vote_a->block;
        }
        else
        {
            // Rep vote remained the same
            result = logos::tally_result::confirm;
        }
    }
    return result;
}

bool logos::votes::uncontested ()
{
    bool result (true);
    if (!rep_votes.empty ())
    {
        auto block (rep_votes.begin ()->second);
        for (auto i (rep_votes.begin ()), n (rep_votes.end ()); result && i != n; ++i)
        {
            result = *i->second == *block;
        }
    }
    return result;
}

// Create a new random keypair
logos::keypair::keypair ()
{
    random_pool.GenerateBlock (prv.data.bytes.data (), prv.data.bytes.size ());
    ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
}

// Create a keypair given a hex string of the private key
logos::keypair::keypair (std::string const & prv_a)
{
    auto error (prv.data.decode_hex (prv_a));
    assert (!error);
    ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
}

// Serialize a block prefixed with an 8-bit typecode
void logos::serialize_block (logos::stream & stream_a, logos::block const & block_a)
{
    write (stream_a, block_a.type ());
    block_a.serialize (stream_a);
}

std::unique_ptr<logos::block> logos::deserialize_block (MDB_val const & val_a)
{
    logos::bufferstream stream (reinterpret_cast<uint8_t const *> (val_a.mv_data), val_a.mv_size);
    return deserialize_block (stream);
}

logos::account_info::account_info ()
    : reservation(0)
    , reservation_epoch(0)
    , head (0)
    , receive_head (0)
    , rep_block (0)
    , open_block (0)
    , balance (0)
    , modified (0)
    , block_count (0)
    , receive_count (0)
{}

logos::account_info::account_info (MDB_val const & val_a)
{
    assert (val_a.mv_size == sizeof (*this));

    static_assert (sizeof (reservation) +
                   sizeof (reservation_epoch) +
                   sizeof (head) +
                   sizeof (receive_head) +
                   sizeof (rep_block) +
                   sizeof (open_block) +
                   sizeof (balance) +
                   sizeof (modified) +
                   sizeof (block_count) +
                   sizeof (receive_count) == sizeof (*this),
                   "Class not packed");

    std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data),
               reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this),
               reinterpret_cast<uint8_t *> (this));
}

logos::account_info::account_info (
        logos::block_hash const & head_a,
        logos::block_hash const & receive_head_a,
        logos::block_hash const & rep_block_a,
        logos::block_hash const & open_block_a,
        logos::amount const & balance_a,
        uint64_t modified_a,
        uint64_t block_count_a,
        uint64_t receive_count_a)
    : reservation(0)
    , reservation_epoch(0)
    , head (head_a)
    , receive_head (receive_head_a)
    , rep_block (rep_block_a)
    , open_block (open_block_a)
    , balance (balance_a)
    , modified (modified_a)
    , block_count (block_count_a)
    , receive_count (receive_count_a)
{}

void logos::account_info::serialize (logos::stream & stream_a) const
{
    write (stream_a, reservation.bytes);
    write (stream_a, reservation_epoch);
    write (stream_a, head.bytes);
    write (stream_a, receive_head.bytes);
    write (stream_a, rep_block.bytes);
    write (stream_a, open_block.bytes);
    write (stream_a, balance.bytes);
    write (stream_a, modified);
    write (stream_a, block_count);
    write (stream_a, receive_count);
}

bool logos::account_info::deserialize (logos::stream & stream_a)
{
    auto error (read (stream_a, reservation.bytes));
    if (!error)
    {
        error = read (stream_a, reservation_epoch);
        if (!error)
        {
            auto error (read (stream_a, head.bytes));
            if (!error)
            {
                error = read (stream_a, receive_head.bytes);
                if (!error)
                {
                    error = read (stream_a, rep_block.bytes);
                    if (!error)
                    {
                        error = read (stream_a, open_block.bytes);
                        if (!error)
                        {
                            error = read (stream_a, balance.bytes);
                            if (!error)
                            {
                                error = read (stream_a, modified);
                                if (!error)
                                {
                                    error = read (stream_a, block_count);
                                    if (!error)
                                    {
                                        error = read (stream_a, receive_count);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return error;
}

bool logos::account_info::operator== (logos::account_info const & other_a) const
{
    return reservation == other_a.reservation &&
           reservation_epoch == other_a.reservation_epoch &&
           head == other_a.head &&
           rep_block == other_a.rep_block &&
           receive_head == other_a.receive_head &&
           open_block == other_a.open_block &&
           balance == other_a.balance &&
           modified == other_a.modified &&
           block_count == other_a.block_count &&
           receive_count == other_a.receive_count;
}

bool logos::account_info::operator!= (logos::account_info const & other_a) const
{
    return !(*this == other_a);
}

logos::mdb_val logos::account_info::val () const
{
    return logos::mdb_val (sizeof (*this), const_cast<logos::account_info *> (this));
}

logos::block_counts::block_counts () :
send (0),
receive (0),
open (0),
change (0),
state (0)
{
}

size_t logos::block_counts::sum ()
{
    return send + receive + open + change + state;
}

logos::pending_info::pending_info () :
source (0),
amount (0)
{
}

logos::pending_info::pending_info (MDB_val const & val_a)
{
    assert (val_a.mv_size == sizeof (*this));
    static_assert (sizeof (source) + sizeof (amount) == sizeof (*this), "Packed class");
    std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

logos::pending_info::pending_info (logos::account const & source_a, logos::amount const & amount_a) :
source (source_a),
amount (amount_a)
{
}

void logos::pending_info::serialize (logos::stream & stream_a) const
{
    logos::write (stream_a, source.bytes);
    logos::write (stream_a, amount.bytes);
}

bool logos::pending_info::deserialize (logos::stream & stream_a)
{
    auto result (logos::read (stream_a, source.bytes));
    if (!result)
    {
        result = logos::read (stream_a, amount.bytes);
    }
    return result;
}

bool logos::pending_info::operator== (logos::pending_info const & other_a) const
{
    return source == other_a.source && amount == other_a.amount;
}

logos::mdb_val logos::pending_info::val () const
{
    return logos::mdb_val (sizeof (*this), const_cast<logos::pending_info *> (this));
}

logos::pending_key::pending_key (logos::account const & account_a, logos::block_hash const & hash_a) :
account (account_a),
hash (hash_a)
{
}

logos::pending_key::pending_key (MDB_val const & val_a)
{
    assert (val_a.mv_size == sizeof (*this));
    static_assert (sizeof (account) + sizeof (hash) == sizeof (*this), "Packed class");
    std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

void logos::pending_key::serialize (logos::stream & stream_a) const
{
    logos::write (stream_a, account.bytes);
    logos::write (stream_a, hash.bytes);
}

bool logos::pending_key::deserialize (logos::stream & stream_a)
{
    auto error (logos::read (stream_a, account.bytes));
    if (!error)
    {
        error = logos::read (stream_a, hash.bytes);
    }
    return error;
}

bool logos::pending_key::operator== (logos::pending_key const & other_a) const
{
    return account == other_a.account && hash == other_a.hash;
}

logos::mdb_val logos::pending_key::val () const
{
    return logos::mdb_val (sizeof (*this), const_cast<logos::pending_key *> (this));
}

logos::block_info::block_info () :
account (0),
balance (0)
{
}

logos::block_info::block_info (MDB_val const & val_a)
{
    assert (val_a.mv_size == sizeof (*this));
    static_assert (sizeof (account) + sizeof (balance) == sizeof (*this), "Packed class");
    std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

logos::block_info::block_info (logos::account const & account_a, logos::amount const & balance_a) :
account (account_a),
balance (balance_a)
{
}

void logos::block_info::serialize (logos::stream & stream_a) const
{
    logos::write (stream_a, account.bytes);
    logos::write (stream_a, balance.bytes);
}

bool logos::block_info::deserialize (logos::stream & stream_a)
{
    auto error (logos::read (stream_a, account.bytes));
    if (!error)
    {
        error = logos::read (stream_a, balance.bytes);
    }
    return error;
}

bool logos::block_info::operator== (logos::block_info const & other_a) const
{
    return account == other_a.account && balance == other_a.balance;
}

logos::mdb_val logos::block_info::val () const
{
    return logos::mdb_val (sizeof (*this), const_cast<logos::block_info *> (this));
}

bool logos::vote::operator== (logos::vote const & other_a) const
{
    return sequence == other_a.sequence && *block == *other_a.block && account == other_a.account && signature == other_a.signature;
}

bool logos::vote::operator!= (logos::vote const & other_a) const
{
    return !(*this == other_a);
}

std::string logos::vote::to_json () const
{
    std::stringstream stream;
    boost::property_tree::ptree tree;
    tree.put ("account", account.to_account ());
    tree.put ("signature", signature.number ());
    tree.put ("sequence", std::to_string (sequence));
    tree.put ("block", block->to_json ());
    boost::property_tree::write_json (stream, tree);
    return stream.str ();
}

logos::amount_visitor::amount_visitor (MDB_txn * transaction_a, logos::block_store & store_a) :
transaction (transaction_a),
store (store_a)
{
}

void logos::amount_visitor::state_block (logos::state_block const & block_a)
{
    balance_visitor prev (transaction, store);
    prev.compute (block_a.hashables.previous);
    result = block_a.hashables.amount.number ();
    result = result < prev.result ? prev.result - result : result - prev.result;
    current = 0;
}

void logos::amount_visitor::compute (logos::block_hash const & block_hash)
{
    current = block_hash;
    while (!current.is_zero ())
    {
        auto block (store.block_get (transaction, current));
        if (block != nullptr)
        {
            block->visit (*this);
        }
        else
        {
            if (block_hash == logos::genesis_account)
            {
                result = std::numeric_limits<logos::uint128_t>::max ();
                current = 0;
            }
            else
            {
                assert (false);
                result = 0;
                current = 0;
            }
        }
    }
}

logos::balance_visitor::balance_visitor (MDB_txn * transaction_a, logos::block_store & store_a) :
transaction (transaction_a),
store (store_a),
current (0),
result (0)
{
}

void logos::balance_visitor::state_block (logos::state_block const & block_a)
{
    result = block_a.hashables.amount.number ();
    current = 0;
}

void logos::balance_visitor::compute (logos::block_hash const & block_hash)
{
    current = block_hash;
    while (!current.is_zero ())
    {
        auto block (store.block_get (transaction, current));
        assert (block != nullptr);
        block->visit (*this);
    }
}

logos::representative_visitor::representative_visitor (MDB_txn * transaction_a, logos::block_store & store_a) :
transaction (transaction_a),
store (store_a),
result (0)
{
}

void logos::representative_visitor::compute (logos::block_hash const & hash_a)
{
    current = hash_a;
    while (result.is_zero ())
    {
        auto block (store.block_get (transaction, current));
        assert (block != nullptr);
        block->visit (*this);
    }
}

void logos::representative_visitor::state_block (logos::state_block const & block_a)
{
    result = block_a.hash ();
}

logos::vote::vote (logos::vote const & other_a) :
sequence (other_a.sequence),
block (other_a.block),
account (other_a.account),
signature (other_a.signature)
{
}

logos::vote::vote (bool & error_a, logos::stream & stream_a)
{
    if (!error_a)
    {
        error_a = logos::read (stream_a, account.bytes);
        if (!error_a)
        {
            error_a = logos::read (stream_a, signature.bytes);
            if (!error_a)
            {
                error_a = logos::read (stream_a, sequence);
                if (!error_a)
                {
                    block = logos::deserialize_block (stream_a);
                    error_a = block == nullptr;
                }
            }
        }
    }
}

logos::vote::vote (bool & error_a, logos::stream & stream_a, logos::block_type type_a)
{
    if (!error_a)
    {
        error_a = logos::read (stream_a, account.bytes);
        if (!error_a)
        {
            error_a = logos::read (stream_a, signature.bytes);
            if (!error_a)
            {
                error_a = logos::read (stream_a, sequence);
                if (!error_a)
                {
                    block = logos::deserialize_block (stream_a, type_a);
                    error_a = block == nullptr;
                }
            }
        }
    }
}

logos::vote::vote (logos::account const & account_a, logos::raw_key const & prv_a, uint64_t sequence_a, std::shared_ptr<logos::block> block_a) :
sequence (sequence_a),
block (block_a),
account (account_a),
signature (logos::sign_message (prv_a, account_a, hash ()))
{
}

logos::vote::vote (MDB_val const & value_a)
{
    logos::bufferstream stream (reinterpret_cast<uint8_t const *> (value_a.mv_data), value_a.mv_size);
    auto error (logos::read (stream, account.bytes));
    assert (!error);
    error = logos::read (stream, signature.bytes);
    assert (!error);
    error = logos::read (stream, sequence);
    assert (!error);
    block = logos::deserialize_block (stream);
    assert (block != nullptr);
}

logos::uint256_union logos::vote::hash () const
{
    logos::uint256_union result;
    blake2b_state hash;
    blake2b_init (&hash, sizeof (result.bytes));
    blake2b_update (&hash, block->hash ().bytes.data (), sizeof (result.bytes));
    union
    {
        uint64_t qword;
        std::array<uint8_t, 8> bytes;
    };
    qword = sequence;
    blake2b_update (&hash, bytes.data (), sizeof (bytes));
    blake2b_final (&hash, result.bytes.data (), sizeof (result.bytes));
    return result;
}

void logos::vote::serialize (logos::stream & stream_a, logos::block_type)
{
    write (stream_a, account);
    write (stream_a, signature);
    write (stream_a, sequence);
    block->serialize (stream_a);
}

void logos::vote::serialize (logos::stream & stream_a)
{
    write (stream_a, account);
    write (stream_a, signature);
    write (stream_a, sequence);
    logos::serialize_block (stream_a, *block);
}

bool logos::vote::deserialize (logos::stream & stream_a)
{
    auto result (read (stream_a, account));
    if (!result)
    {
        result = read (stream_a, signature);
        if (!result)
        {
            result = read (stream_a, sequence);
            if (!result)
            {
                block = logos::deserialize_block (stream_a, block_type ());
                result = block == nullptr;
            }
        }
    }
    return result;
}

logos::genesis::genesis ()
{
    boost::property_tree::ptree tree;
    std::stringstream istream (logos::genesis_block);
    boost::property_tree::read_json (istream, tree);
    auto block (logos::deserialize_block_json (tree));
    //CH Logos is not using this genesis anymore
    //assert (dynamic_cast<logos::open_block *> (block.get ()) != nullptr);
    //CH open.reset (static_cast<logos::open_block *> (block.release ()));
}

void logos::genesis::initialize (MDB_txn * transaction_a, logos::block_store & store_a) const
{
    auto hash_l (hash ());
    assert (store_a.latest_begin (transaction_a) == store_a.latest_end ());
    //CH store_a.block_put (transaction_a, hash_l, *open);
    //CH store_a.account_put (transaction_a, genesis_account, { hash_l, open->hash (), open->hash (), std::numeric_limits<logos::uint128_t>::max (), logos::seconds_since_epoch (), 1 });
    store_a.representation_put (transaction_a, genesis_account, std::numeric_limits<logos::uint128_t>::max ());
    store_a.checksum_put (transaction_a, 0, 0, hash_l);
    store_a.frontier_put (transaction_a, hash_l, genesis_account);
}

logos::block_hash logos::genesis::hash () const
{
    return 0;//CH open->hash ();
}

std::string logos::ProcessResultToString(logos::process_result result)
{
    std::string ret;

    switch(result)
    {
    case process_result::progress:
        ret = "Progress";
        break;
    case process_result::bad_signature:
        ret = "Bad Signature";
        break;
    case process_result::old:
        ret = "Old Block";
        break;
    case process_result::negative_spend:
        ret = "Negative Spend";
        break;
    case process_result::fork:
        ret = "Fork";
        break;
    case process_result::unreceivable:
        ret = "Unreceivable";
        break;
    case process_result::gap_previous:
        ret = "Gap Previous Block";
        break;
    case process_result::gap_source:
        ret = "Gap Source Block";
        break;
    case process_result::state_block_disabled:
        ret = "State Blocks Are Disabled";
        break;
    case process_result::not_receive_from_send:
        ret = "Not Receive From Send";
        break;
    case process_result::account_mismatch:
        ret = "Account Mismatch";
        break;
    case process_result::opened_burn_account:
        ret = "Invalid account (burn account)";
        break;
    case process_result::balance_mismatch:
        ret = "Balance Mismatch";
        break;
    case process_result::block_position:
        ret = "Block Position";
        break;
    case process_result::invalid_block_type:
        ret = "Invalid Block Type";
        break;
    case process_result::unknown_source_account:
        ret = "Unknown Source Account";
        break;
    case process_result::buffered:
        ret = "Buffered";
        break;
    case process_result::buffering_done:
        ret = "Buffering Done";
        break;
    case process_result::pending:
        ret = "Already Pending";
        break;
    case process_result::already_reserved:
        ret = "Account already Reserved";
        break;
    case process_result::initializing:
        ret = "Delegate is initializing";
        break;
    case process_result::insufficient_fee:
        ret = "Transaction fee is insufficient";
        break;
    case process_result::insufficient_balance:
        ret = "Account balance is insufficient";
        break;
    case process_result::not_delegate:
         ret = "Not a delegate";
        break;
    case process_result::clock_drift:
        ret = "Clock drift";
        break;
    case process_result::wrong_sequence_number:
        ret = "Wrong sequence number";
        break;
    case process_result::invalid_request:
        ret = "Invalid request";
        break;
    case process_result::invalid_tip:
        ret = "Invalid tip";
        break;
    case process_result::invalid_number_blocks:
        ret = "Invalid number blocks";
        break;
    }

    return ret;
}
