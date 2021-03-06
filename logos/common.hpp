#pragma once

#include <logos/lib/ecies.hpp>
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
    //If you call this function, you must also store the modified Account struct in
    //account_db (by calling account_put). Otherwise, the account_db and
    //voting_power_db will become out of sync
    //This comment does not apply to token accounts
    virtual void SetBalance(
            amount const & new_balance,
            uint32_t const & epoch,
            MDB_txn* txn) = 0;

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

struct RepRecord
{
    //0 means no rep. Note, reps themselves have this field set to 0
    AccountAddress rep = 0;
    uint32_t epoch_first_active = 0;
    uint32_t Serialize(stream &stream) const
    {
        auto s = 0;
        s += write(stream,rep);
        s += write(stream,epoch_first_active);
        return s;
    }
    bool Deserialize(stream &stream)
    {
        return read(stream, rep)|| read(stream, epoch_first_active);
    }
    bool operator==(RepRecord const & other) const
    {
        return rep == other.rep && epoch_first_active == other.epoch_first_active;
    }

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
                  uint32_t receive_count,
                  uint32_t claim_epoch);

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
    //If you call this function, you must also store the modified Account struct in
    //account_db (by calling account_put). Otherwise, the account_db and
    //voting_power_db will become out of sync
    void SetBalance(
            amount const & new_balance,
            uint32_t const & epoch,
            MDB_txn* txn) override;

    void SetAvailableBalance(
            amount const & new_available_bal,
            uint32_t const & epoch,
            MDB_txn* txn);

    AccountAddress GetRepForEpoch(uint32_t epoch) const;

    Rational GetFullAvailableBalance() const;
    amount const & GetAvailableBalance() const override;
    amount const & GetBalance() const override;

    block_hash governance_subchain_head;
    RepRecord old_rep;
    RepRecord new_rep;
    block_hash open_block;
    Entries    entries;
    //the last epoch in which thawing funds were checked for expiration for this account
    uint32_t   epoch_thawing_updated;
    //the last epoch in which secondary liabilities were checked for expiration for this account
    uint32_t   epoch_secondary_liabilities_updated;
    uint32_t   claim_epoch;
    Rational   dust;

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

// TODO: Remove unused enums and perhaps separate
//       these enums based on the validation class.
//
enum class process_result
{
    progress,                     // Hasn't been seen before, signed correctly
    propagate,                    // Valid but sent to non-delegate. Propagating via p2p
    no_propagate,                 // Valid, sent to non-delegate, but not propagated
    bad_signature,                // Signature was bad, forged or transmission error
    old,                          // Already seen and was valid
    negative_spend,               // Malicious attempt to spend a negative amount
    fork,                         // Malicious fork based on previous
    unreceivable,                 // Source block doesn't exist or has already been received
    gap_previous,                 // Block marked as previous is unknown
    gap_source,                   // Block marked as source is unknown
    state_block_disabled,         // Awaiting state block canary block
    not_receive_from_send,        // Receive does not have a send source
    account_mismatch,             // Account number in open block doesn't match send destination
    opened_burn_account,          // The impossible happened, someone found the private key associated with the public key '0'.
    balance_mismatch,             // Balance and amount delta don't match
    block_position,               // This block cannot follow the previous block
    invalid_block_type,           // Logos - Only allow state blocks
    unknown_source_account,       // Logos - The source account is unknown.
    unknown_origin,               // Logos - The sender's account is unknown.
    buffered,                     // Logos - The block has been buffered for benchmarking.
    buffering_done,               // Logos - The last block has been buffered and consensus will begin.
    pending,                      // Logos - The block has already been received and is pending consensus.
    already_reserved,             // Logos - The account is already reserved with different request.
    initializing,                 // Logos - The delegate is initializing and not accepting transactions.
    insufficient_fee,             // Logos - Transaction fee is insufficient.
    insufficient_balance,         // Logos - Balance is insufficient.
    not_delegate,                 // Logos - A non-delegate node rejects transaction request, or invalid delegate in epoch block
    clock_drift,                  // Logos - timestamp exceeds allowed clock drift
    wrong_sequence_number,        // Logos - invalid block sequence number
    invalid_request,              // Logos - An incoming request is invalid.
    invalid_tip,                  // Logos - invalid microblock tip
    invalid_number_blocks,        // Logos - invalid number of blocks in microblock
    revert_immutability,          // Logos - Attempting to change a token account mutability setting from false to true
    immutable,                    // Logos - Attempting to update an immutable token account setting
    redundant,                    // Logos - The token account setting change was idempotent
    insufficient_token_balance,   // Logos - Token balance is insufficient.
    invalid_token_id,             // Logos - Token ID is invalid.
    untethered_account,           // Logos - User account has not been tethered to the specified token account.
    invalid_controller,           // Logos - An invalid controller was specified.
    controller_capacity,          // Logos - No more controllers can be added.
    invalid_controller_action,    // Logos - An invalid controller action was specified.
    unauthorized_request,         // Logos - Unauthorized to make request.
    prohibitted_request,          // Logos - The request is not allowed.
    not_whitelisted,              // Logos - Whitelisting is required.
    frozen,                       // Logos - Account is frozen.
    insufficient_token_fee,       // Logos - Token fee is insufficient.
    invalid_token_symbol,         // Logos - Token symbol is invalid.
    invalid_token_name,           // Logos - Token name is invalid.
    invalid_token_amount,         // Logos - Token amount is invalid.
    total_supply_overflow,        // Logos - The request would case the token total supply to overflow.
    key_collision,                // Logos - There is already a user account or token account with the same key.
    invalid_fee,                  // Logos - The fee settings are invalid.
    invalid_issuer_info,          // Logos - The issuer info supplied is invalid.
    too_many_token_entries,       // Logos - The account has too many token entries.
    elections_dead_period,        // Logos - the time between epoch start and epoch block post-commit
    not_a_rep,                    // Logos - the account is not a representative
    already_voted,                // Logos - the rep already voted this epoch
    invalid_candidate,            // Logos - the vote is for an account that is not a candidate
    not_enough_stake,             // Logos - the rep does not have enough stake for action
    never_announced_candidacy,    // Logos - the rep has never announced candidacy
    already_renounced_candidacy,  // Logos - the rep is already in a renounced candidacy state
    already_announced_candidacy,  // Logos - the rep is already in an announced candidacy state
    is_rep,                       // Logos - the account is a representative
    is_candidate,                 // Logos - the account is a candidate
    is_delegate,                  // Logos - the account is a delegate
    wrong_epoch_number,           // Logos - the request has an incorrect epoch number
    no_elections,                 // Logos - elections are not being held currently
    pending_rep_action,           // Logos - the account has a pending representative action for this epoch
    pending_candidacy_action,     // Logos - the account has a pending candidacy action for this epoch
    invalid_governance_subchain,  // Logos - hash sent as governance_subchain_prev does not match governance_subchain_head of account
    insufficient_funds_for_stake, // Logos - not enough available funds to satisfy stake request
    invalid_account_type,         // Logos - origin account is not the proper type for the request
    proxy_to_self,                // Logos - request is attempting to proxy to self
    invalid_epoch_hash            // Logos - invalid epoch hash
};

// This enum represents type of dependency which we need to wait in the cache, type is based on the returned error code
// See second column in the document "Error codes in block validation of full nodes"
enum class process_result_dependency
{
    progress,                     // Good
    not_applied,                  // Not applied, bad_signature
    bad_block,                    // Bad block, not recoverable error
    general_error_code,           // See individual error codes of requests in RB or request tips in MB
    previous_block,               // Dependency is previous block in the chain
    sender_account,               // Dependency is sender account as a transaction target
    last_microblock,              // Dependency is last microblock of the epoch
    previous_epoch                // Dependency is previous epoch, delegate set is needed to check signatures
};

std::string ProcessResultToString(process_result result);
bool MissingBlock(process_result result);

process_result_dependency ProcessResultToDependency(process_result result);

class process_return
{
public:
    logos::process_result code;
    logos::account account;
    logos::amount amount;
};

struct genesis_delegate
{
   logos::public_key  key; ///< EDDSA key for signing Micro/Epoch blocks (TBD, should come from wallet)
   bls::PublicKey  bls_pub;
   ECIESPublicKey  ecies_pub;
   Amount          vote;
   Amount          stake;
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
extern std::vector<logos::public_key> genesis_delegates;
// A block hash that compares inequal to any real block hash
extern logos::block_hash const & not_a_block;
// An account number that compares inequal to any real account number
extern logos::block_hash const & not_an_account;

class node;
}

namespace logos_global
{
    /*
     * Bootstrap is best effort.
     * A "Completed" result only means most likely catch up with the network.
     */
    enum BootstrapResult{
        Completed,                       // good
        NoNode,                          // no node object
        BootstrapInitiatorStopped,       // BootstrapInitiator stopped
        Incomplete                       // gived up
    };
    std::string BootstrapResultToString(BootstrapResult result);

    using BootstrapCompleteCB = std::function<void(BootstrapResult)>;

    void AssignNode(std::shared_ptr<logos::node> &n);
    std::shared_ptr<logos::node> GetNode();
    void Bootstrap(BootstrapCompleteCB cb = {});
}
