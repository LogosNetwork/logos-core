#pragma once

#include <logos/lib/ecies.hpp>
#include <logos/lib/blocks.hpp>
#include <logos/token/entry.hpp>
#include <logos/node/utility.hpp>

#include <boost/property_tree/ptree.hpp>

#include <unordered_map>
#include <vector>

#include <blake2/blake2.h>

#include <bls/bls.hpp>

namespace boost
{
template <>
struct hash<logos::uint256_union>
{
    size_t operator() (logos::uint256_union const & value_a) const
    {
        std::hash<logos::uint256_union> hash;
        return hash (value_a);
    }
};
}
namespace logos
{
const uint8_t protocol_version = 0x0a;
const uint8_t protocol_version_min = 0x07;

class block_store;
/**
 * Determine the balance as of this block
 */
class balance_visitor : public logos::block_visitor
{
public:
    balance_visitor (MDB_txn *, logos::block_store &);
    virtual ~balance_visitor () = default;
    void compute (logos::block_hash const &);
    void state_block (logos::state_block const &) override;
    MDB_txn * transaction;
    logos::block_store & store;
    logos::block_hash current;
    logos::uint128_t result;
};

/**
 * Determine the amount delta resultant from this block
 */
class amount_visitor : public logos::block_visitor
{
public:
    amount_visitor (MDB_txn *, logos::block_store &);
    virtual ~amount_visitor () = default;
    void compute (logos::block_hash const &);
    void state_block (logos::state_block const &) override;
    void from_send (logos::block_hash const &);
    MDB_txn * transaction;
    logos::block_store & store;
    logos::block_hash current;
    logos::uint128_t result;
};

/**
 * Determine the representative for this block
 */
class representative_visitor : public logos::block_visitor
{
public:
    representative_visitor (MDB_txn * transaction_a, logos::block_store & store_a);
    virtual ~representative_visitor () = default;
    void compute (logos::block_hash const & hash_a);
    void state_block (logos::state_block const & block_a) override;
    MDB_txn * transaction;
    logos::block_store & store;
    logos::block_hash current;
    logos::block_hash result;
};

/**
 * A key pair. The private key is generated from the random pool, or passed in
 * as a hex string. The public key is derived using ed25519.
 */
class keypair
{
public:
    keypair ();
    keypair (std::string const &);
    keypair (const keypair &k) { pub = k.pub; prv.data = k.prv.data; }
    logos::public_key pub;
    logos::raw_key prv;
};

std::unique_ptr<logos::block> deserialize_block (MDB_val const &);

enum class AccountType : uint8_t
{
    LogosAccount = 0,
    TokenAccount = 1
};

struct Account
{
public:

    Account() = default;

    Account(AccountType type);
    Account(bool & error, const mdb_val & mdbval);
    Account(bool & error, logos::stream & stream);

    Account(AccountType type,
            amount const & balance,
            uint64_t modified,
            block_hash const & head,
            uint32_t block_count,
            const BlockHash & receive_head,
            uint32_t receive_count);

    virtual uint32_t Serialize(logos::stream &) const;
    virtual bool Deserialize(logos::stream &);
    bool operator== (Account const &) const;
    bool operator!= (Account const &) const;

    virtual mdb_val to_mdb_val(std::vector<uint8_t> &buf) const = 0;
    
    //IMPORTANT!: Calling this function persists the change in voting_power_db
    //If you call this function, you must also store the new Account struct in
    //account_db (by calling account_put). Otherwise, the account_db and
    //voting_power_db will become out of sync
    //This comment does not apply to token accounts
    virtual void SetBalance(
            amount const & new_balance,
            uint32_t const & epoch,
            MDB_txn* txn,
            bool persist = true) = 0;

    virtual amount const & GetBalance() const = 0;
    virtual amount const & GetAvailableBalance() const = 0;

    AccountType type;
    uint64_t    modified;      ///< Seconds since posix epoch
    BlockHash   head;
    uint32_t    block_count;
    BlockHash   receive_head;
    uint32_t    receive_count;

    protected:
    amount balance;
};

/**
 * Latest information about an account
 */
struct account_info : Account
{
    using Entries = std::vector<TokenEntry>;

    account_info ();
    account_info (bool & error, const mdb_val & mdbval);
    account_info (bool & error, logos::stream & stream);
    account_info (account_info const &) = default;

    account_info (block_hash const & head,
                  block_hash const & receive_head,
                  block_hash const & staking_subchain_head,
                  block_hash const & open_block,
                  amount const & balance,
                  uint64_t modified,
                  uint32_t block_count,
                  uint32_t receive_count);

    uint32_t Serialize(stream &stream_a) const override;
    bool Deserialize(stream &stream_a) override;
    bool operator== (account_info const &) const;
    bool operator!= (account_info const &) const;
    //mdb_val val () const;
    mdb_val to_mdb_val(std::vector<uint8_t> &buf) const override;

    bool GetEntry(const BlockHash & token_id, TokenEntry & val) const;
    Entries::iterator GetEntry(const BlockHash & token_id);

    static constexpr uint16_t MAX_TOKEN_ENTRIES = std::numeric_limits<uint16_t>::max();

    //IMPORTANT!: Calling this function persists the change in voting_power_db
    //If you call this function, you must also store the new Account struct in
    //account_db (by calling account_put). Otherwise, the account_db and
    //voting_power_db will become out of sync
    void SetBalance(
            amount const & new_balance,
            uint32_t const & epoch,
            MDB_txn* txn,
            bool persist = true) override;

    void SetAvailableBalance(
            amount const & new_available_bal,
            uint32_t const & epoch,
            MDB_txn* txn,
            bool persist = true);

    amount const & GetAvailableBalance() const override;
    amount const & GetBalance() const override;

    block_hash staking_subchain_head;
    block_hash open_block;
    Entries    entries;
    //the last epoch in which thawing funds were checked for expiration for this account
    uint32_t   epoch_thawing_updated;
    //the last epoch in which secondary liablities were checked for expiration for this account
    uint32_t   epoch_secondary_liabilities_updated;

    protected:
    amount available_balance;
};

std::shared_ptr<Account> DeserializeAccount(bool & error, const logos::mdb_val & mdbval);
std::shared_ptr<Account> DeserializeAccount(bool & error, logos::stream & stream);

/**
 * Latest information about an account reservation, if any exists
 */
struct reservation_info
{
    reservation_info ();
    reservation_info (bool & error, const logos::mdb_val & mdbval);
    reservation_info (logos::reservation_info const &) = default;

    reservation_info (logos::block_hash const & reservation,
                      uint32_t const & reservation_epoch);

    uint32_t serialize (logos::stream &) const;
    bool Deserialize(logos::stream &stream_a);
    bool operator== (logos::reservation_info const &) const;
    bool operator!= (logos::reservation_info const &) const;
    logos::mdb_val to_mdb_val(std::vector<uint8_t> &buf) const;

    logos::block_hash   reservation;        /// the transaction hash for which the account was reserved
    uint32_t            reservation_epoch;  /// epoch in which account was reserved
};

/**
 * Information on an uncollected send
 */
class pending_info
{
public:
    pending_info ();
    pending_info (MDB_val const &);
    pending_info (logos::account const &, logos::amount const &);
    void serialize (logos::stream &) const;
    bool deserialize (logos::stream &);
    bool operator== (logos::pending_info const &) const;
    logos::mdb_val val () const;
    logos::account source;
    logos::amount amount;
};
class pending_key
{
public:
    pending_key (logos::account const &, logos::block_hash const &);
    pending_key (MDB_val const &);
    void serialize (logos::stream &) const;
    bool deserialize (logos::stream &);
    bool operator== (logos::pending_key const &) const;
    logos::mdb_val val () const;
    logos::account account;
    logos::block_hash hash;
};
class block_info
{
public:
    block_info ();
    block_info (MDB_val const &);
    block_info (logos::account const &, logos::amount const &);
    void serialize (logos::stream &) const;
    bool deserialize (logos::stream &);
    bool operator== (logos::block_info const &) const;
    logos::mdb_val val () const;
    logos::account account;
    logos::amount balance;
};
class block_counts
{
public:
    block_counts ();
    size_t sum ();
    size_t send;
    size_t receive;
    size_t open;
    size_t change;
    size_t state;
};
class vote
{
public:
    vote () = default;
    vote (logos::vote const &);
    vote (bool &, logos::stream &);
    vote (bool &, logos::stream &, logos::block_type);
    vote (logos::account const &, logos::raw_key const &, uint64_t, std::shared_ptr<logos::block>);
    vote (MDB_val const &);
    logos::uint256_union hash () const;
    bool operator== (logos::vote const &) const;
    bool operator!= (logos::vote const &) const;
    void serialize (logos::stream &, logos::block_type);
    void serialize (logos::stream &);
    bool deserialize (logos::stream &);
    std::string to_json () const;
    // Vote round sequence number
    uint64_t sequence;
    std::shared_ptr<logos::block> block;
    // Account that's voting
    logos::account account;
    // Signature of sequence + block hash
    logos::signature signature;
};
enum class vote_code
{
    invalid, // Vote is not signed correctly
    replay, // Vote does not have the highest sequence number, it's a replay
    vote // Vote has the highest sequence number
};

// TODO: Remove unused enums and perhaps separate
//       these enums based on the validation class.
//
enum class process_result
{
    progress,                   // Hasn't been seen before, signed correctly
    bad_signature,              // Signature was bad, forged or transmission error
    old,                        // Already seen and was valid
    negative_spend,             // Malicious attempt to spend a negative amount
    fork,                       // Malicious fork based on previous
    unreceivable,               // Source block doesn't exist or has already been received
    gap_previous,               // Block marked as previous is unknown
    gap_source,                 // Block marked as source is unknown
    state_block_disabled,       // Awaiting state block canary block
    not_receive_from_send,      // Receive does not have a send source
    account_mismatch,           // Account number in open block doesn't match send destination
    opened_burn_account,        // The impossible happened, someone found the private key associated with the public key '0'.
    balance_mismatch,           // Balance and amount delta don't match
    block_position,             // This block cannot follow the previous block
    invalid_block_type,         // Logos - Only allow state blocks
    unknown_source_account,     // Logos - The source account is unknown.
    unknown_origin,             // Logos - The sender's account is unknown.
    buffered,                   // Logos - The block has been buffered for benchmarking.
    buffering_done,             // Logos - The last block has been buffered and consensus will begin.
    pending,                    // Logos - The block has already been received and is pending consensus.
    already_reserved,           // Logos - The account is already reserved with different request.
    initializing,               // Logos - The delegate is initializing and not accepting transactions.
    insufficient_fee,           // Logos - Transaction fee is insufficient.
    insufficient_balance,       // Logos - Balance is insufficient.
    not_delegate,               // Logos - A non-delegate node rejects transaction request, or invalid delegate in epoch block
    clock_drift,                // Logos - timestamp exceeds allowed clock drift
    wrong_sequence_number,      // Logos - invalid block sequence number
    invalid_request,            // Logos - An incoming request is invalid.
    invalid_tip,                // Logos - invalid microblock tip
    invalid_number_blocks,      // Logos - invalid number of blocks in microblock
    revert_immutability,        // Logos - Attempting to change a token account mutability setting from false to true
    immutable,                  // Logos - Attempting to update an immutable token account setting
    redundant,                  // Logos - The token account setting change was idempotent
    insufficient_token_balance, // Logos - Token balance is insufficient.
    invalid_token_id,           // Logos - Token ID is invalid.
    untethered_account,         // Logos - User account has not been tethered to the specified token account.
    invalid_controller,         // Logos - An invalid controller was specified.
    controller_capacity,        // Logos - No more controllers can be added.
    invalid_controller_action,  // Logos - An invalid controller action was specified.
    unauthorized_request,       // Logos - Unauthorized to make request.
    prohibitted_request,        // Logos - The request is not allowed.
    not_whitelisted,            // Logos - Whitelisting is required.
    frozen,                     // Logos - Account is frozen.
    insufficient_token_fee,     // Logos - Token fee is insufficient.
    invalid_token_symbol,       // Logos - Token symbol is invalid.
    invalid_token_name,         // Logos - Token name is invalid.
    invalid_token_amount,       // Logos - Token amount is invalid.
    total_supply_overflow,      // Logos - The request would case the token total supply to overflow.
    key_collision,              // Logos - There is already a user account or token account with the same key.
    invalid_fee,                // Logos - The fee settings are invalid.
    invalid_issuer_info,        // Logos - The issuer info supplied is invalid.
    too_many_token_entries,     // Logos - The account has too many token entries.
    elections_dead_period,      // Logos - the time between epoch start and epoch block post-commit
    not_a_rep,                  // Logos - the account is not a representative
    already_voted,              // Logos - the rep already voted this epoch
    invalid_candidate,          // Logos - the vote is for an account that is not a candidate
    not_enough_stake,           // Logos - the rep does not have enough stake for action
    never_announced_candidacy,  // Logos - the rep has never announced candidacy
    already_renounced_candidacy,// Logos - the rep is already in a renounced candidacy state
    already_announced_candidacy,// Logos - the rep is already in an announced candidacy state
    is_rep,                     // Logos - the account is a representative
    wrong_epoch_number,         // Logos - the request has an incorrect epoch number
    no_elections,               // Logos - elections are not being held currently
    pending_rep_action,         // Logos - the account has a pending representative action for this epoch
    pending_candidacy_action    // Logos - the account has a pending candidacy action for this epoch
};

std::string ProcessResultToString(process_result result);

class process_return
{
public:
    logos::process_result code;
    logos::account account;
    logos::amount amount;
    logos::account pending_account;
    boost::optional<bool> state_is_send;
};
enum class tally_result
{
    vote,
    changed,
    confirm
};
class votes
{
public:
    votes (std::shared_ptr<logos::block>);
    logos::tally_result vote (std::shared_ptr<logos::vote>);
    bool uncontested ();
    // Root block of fork
    logos::block_hash id;
    // All votes received by account
    std::unordered_map<logos::account, std::shared_ptr<logos::block>> rep_votes;
};
struct genesis_delegate
{
   logos::keypair  key; ///< EDDSA key for signing Micro/Epoch blocks (TBD, should come from wallet)
   bls::KeyPair    bls_key;
   ECIESKeyPair    ecies_key;
   uint64_t        vote;
   uint64_t        stake;
};
extern logos::keypair const & zero_key;
extern logos::keypair const & test_genesis_key;
extern logos::account const & logos_test_account;
extern logos::account const & logos_beta_account;
extern logos::account const & logos_live_account;
extern std::string const & logos_test_genesis;
extern std::string const & logos_beta_genesis;
extern std::string const & logos_live_genesis;
extern std::string const & genesis_block;
extern logos::account const & genesis_account;
extern logos::account const & burn_account;
extern logos::uint128_t const & genesis_amount;
extern std::vector<genesis_delegate> genesis_delegates;
// A block hash that compares inequal to any real block hash
extern logos::block_hash const & not_a_block;
// An account number that compares inequal to any real account number
extern logos::block_hash const & not_an_account;
class genesis
{
public:
    explicit genesis ();
    void initialize (MDB_txn *, logos::block_store &) const;
    logos::block_hash hash () const;
    //CH std::unique_ptr<logos::open_block> open;
};

class node;
}

namespace logos_global
{
    void AssignNode(std::shared_ptr<logos::node> &n);
    std::shared_ptr<logos::node> GetNode();
    void Bootstrap();
}
