#pragma once

#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/messages/common.hpp>
#include <logos/microblock/microblock.hpp>
#include <logos/token/account.hpp>
#include <logos/epoch/epoch.hpp>
#include <logos/lib/log.hpp>
#include <logos/common.hpp>

namespace logos
{
/**
 * The value produced when iterating with \ref store_iterator
 */
class store_entry
{
public:
    store_entry ();
    void clear ();
    store_entry * operator-> ();
    logos::mdb_val first;
    logos::mdb_val second;
};

/**
 * Iterates the key/value pairs of a transaction
 */
class store_iterator
{
public:
    store_iterator (MDB_txn *, MDB_dbi);
    store_iterator (std::nullptr_t);
    store_iterator (MDB_txn *, MDB_dbi, MDB_val const &);
    store_iterator (logos::store_iterator &&);
    store_iterator (logos::store_iterator const &) = delete;
    ~store_iterator ();
    logos::store_iterator & operator++ ();
    void next_dup ();
    logos::store_iterator & operator= (logos::store_iterator &&);
    logos::store_iterator & operator= (logos::store_iterator const &) = delete;
    logos::store_entry & operator-> ();
    bool operator== (logos::store_iterator const &) const;
    bool operator!= (logos::store_iterator const &) const;
    MDB_cursor * cursor;
    logos::store_entry current;
};

/**
 * Manages block storage and iteration
 */
class block_store
{
public:
    block_store (bool &, boost::filesystem::path const &, int lmdb_max_dbs = 128);

    MDB_dbi block_database (logos::block_type);
    void block_put_raw (MDB_txn *, MDB_dbi, logos::block_hash const &, MDB_val);
    void block_put (MDB_txn *, logos::block_hash const &, logos::block const &, logos::block_hash const & = logos::block_hash (0));
    MDB_val block_get_raw (MDB_txn *, logos::block_hash const &, logos::block_type &);
    logos::block_hash block_successor (MDB_txn *, logos::block_hash const &);
    void block_successor_clear (MDB_txn *, logos::block_hash const &);
    std::unique_ptr<logos::block> block_get (MDB_txn *, logos::block_hash const &);
    //CH std::unique_ptr<logos::block> block_random (MDB_txn *);
    //CH std::unique_ptr<logos::block> block_random (MDB_txn *, MDB_dbi);
    void block_del (MDB_txn *, logos::block_hash const &);
    bool block_exists (MDB_txn *, logos::block_hash const &);
    logos::block_counts block_count (MDB_txn *);
    bool root_exists (MDB_txn *, logos::uint256_union const &);

    void frontier_put (MDB_txn *, logos::block_hash const &, logos::account const &);
    logos::account frontier_get (MDB_txn *, logos::block_hash const &);
    void frontier_del (MDB_txn *, logos::block_hash const &);

    void account_put (MDB_txn *, logos::account const &, logos::account_info const &);
    bool account_get (MDB_txn *, logos::account const &, logos::account_info &);
    bool account_get (MDB_txn *, logos::account const &, logos::account_info &, MDB_dbi);
    void account_del (MDB_txn *, logos::account const &);
    bool account_exists (MDB_txn *, logos::account const &);
    size_t account_count (MDB_txn *);
    logos::store_iterator latest_begin (MDB_txn *, logos::account const &);
    logos::store_iterator latest_begin (MDB_txn *);
    logos::store_iterator latest_end ();

    void pending_put (MDB_txn *, logos::pending_key const &, logos::pending_info const &);
    void pending_del (MDB_txn *, logos::pending_key const &);
    bool pending_get (MDB_txn *, logos::pending_key const &, logos::pending_info &);
    bool pending_exists (MDB_txn *, logos::pending_key const &);
    logos::store_iterator pending_begin (MDB_txn *, logos::pending_key const &);
    logos::store_iterator pending_begin (MDB_txn *);
    logos::store_iterator pending_end ();

    void block_info_put (MDB_txn *, logos::block_hash const &, logos::block_info const &);
    void block_info_del (MDB_txn *, logos::block_hash const &);
    bool block_info_get (MDB_txn *, logos::block_hash const &, logos::block_info &);
    bool block_info_exists (MDB_txn *, logos::block_hash const &);
    logos::store_iterator block_info_begin (MDB_txn *, logos::block_hash const &);
    logos::store_iterator block_info_begin (MDB_txn *);
    logos::store_iterator block_info_end ();
    logos::uint128_t block_balance (MDB_txn *, logos::block_hash const &);
    static size_t const block_info_max = 32;

    logos::uint128_t representation_get (MDB_txn *, logos::account const &);
    void representation_put (MDB_txn *, logos::account const &, logos::uint128_t const &);
    void representation_add (MDB_txn *, logos::account const &, logos::uint128_t const &);
    logos::store_iterator representation_begin (MDB_txn *);
    logos::store_iterator representation_end ();

    void unchecked_clear (MDB_txn *);
    void unchecked_put (MDB_txn *, logos::block_hash const &, std::shared_ptr<logos::block> const &);
    std::vector<std::shared_ptr<logos::block>> unchecked_get (MDB_txn *, logos::block_hash const &);
    void unchecked_del (MDB_txn *, logos::block_hash const &, logos::block const &);
    logos::store_iterator unchecked_begin (MDB_txn *);
    logos::store_iterator unchecked_begin (MDB_txn *, logos::block_hash const &);
    logos::store_iterator unchecked_end ();
    size_t unchecked_count (MDB_txn *);
    std::unordered_multimap<logos::block_hash, std::shared_ptr<logos::block>> unchecked_cache;

    template<typename T> void put(MDB_dbi&, const mdb_val &, const T &, MDB_txn *);
    template<typename T> void put(MDB_dbi& db, const Byte32Array &key_32b, const T &t, MDB_txn *tx)
    {
        mdb_val key(key_32b);
        put<T>(db, key, t, tx);
    }
    template<typename T> logos::block_hash put(MDB_dbi&, const T &, MDB_txn *);
    template<typename T> bool get(MDB_dbi&, const mdb_val &key, T &, MDB_txn *tx = nullptr);
    template<typename T> bool get(MDB_dbi& db, const Byte32Array &key_32b, T &t, MDB_txn *tx = nullptr)
    {
        mdb_val key(key_32b);
        return get<T>(db,key,t,tx);
    }
    void del(MDB_dbi&, const mdb_val &, MDB_txn * tx);
    void del(MDB_dbi& db, const Byte32Array &key_32b, MDB_txn *tx)
    {
        mdb_val key(key_32b);
        del(db, key, tx);
    }

    //////////////////

    // abstract away consensus types
    bool consensus_block_get (const BlockHash & hash, ApprovedRB & block);
    bool consensus_block_get (const BlockHash & hash, ApprovedMB & block);
    bool consensus_block_get (const BlockHash & hash, ApprovedEB & block);
    // return true if cannot found hash
    bool consensus_block_update_next(const BlockHash & hash, const BlockHash & next, ConsensusType type, MDB_txn * transaction);
    
    bool request_block_put(ApprovedRB const & block, MDB_txn * transaction);
    bool request_block_put(ApprovedRB const & block, const BlockHash & hash, MDB_txn *transaction);
    bool request_block_get(const BlockHash & hash, ApprovedRB & block);
    bool request_block_get(const BlockHash &hash, ApprovedRB &block, MDB_txn *);

    bool request_get(const BlockHash &hash, Request & request, MDB_txn *transaction);
    bool request_get(const BlockHash &hash, std::shared_ptr<Request> & request, MDB_txn *transaction);
    bool request_put(const Request &, const BlockHash &, MDB_txn *);
    bool request_exists(const Request & request);
    bool request_exists(const BlockHash & hash);

    bool token_account_exists(const BlockHash & token_id);
    bool token_account_get(AccountAddress const & account_a, std::shared_ptr<Account> & info_a, MDB_txn* t=0);
    bool token_account_get(AccountAddress const & account_a, TokenAccount & info_a, MDB_txn* t=0);
    bool token_account_db_empty();
    bool token_account_put (AccountAddress const &, TokenAccount const &, MDB_txn *);

    bool account_get(AccountAddress const & account_a, std::shared_ptr<Account> & info_a, AccountType type, MDB_txn* t=0);
    bool account_get(AccountAddress const & account_a, std::shared_ptr<Account> & info_a, MDB_txn* t=0);
    bool account_get(AccountAddress const & account_a, account_info & info_a, MDB_txn* t=0);
    bool account_db_empty();
    bool account_put (AccountAddress const &, logos::account_info const &, MDB_txn *);

    bool receive_put(const BlockHash & hash, const ReceiveBlock & block, MDB_txn *);
    bool receive_get(const BlockHash & hash, ReceiveBlock & block, MDB_txn *);
    bool receive_exists(const BlockHash & hash);

    bool batch_tip_put(uint8_t delegate_id, const BlockHash & hash, MDB_txn *);
    bool batch_tip_get(uint8_t delegate_id, BlockHash & hash);

    // micro-block
    bool get(MDB_dbi &db, const mdb_val &key, mdb_val &value, MDB_txn *tx);
    bool micro_block_put(ApprovedMB const &, MDB_txn*);
    bool micro_block_get(const BlockHash &, ApprovedMB &, MDB_txn* t=0);
    bool micro_block_tip_put(const BlockHash &, MDB_txn*);
    bool micro_block_tip_get(BlockHash &, MDB_txn* t=0);
    bool micro_block_exists(const BlockHash &, MDB_txn* t=0);
    bool micro_block_exists(const ApprovedMB &);

    // epoch
    bool epoch_put(ApprovedEB const &, MDB_txn*);
    bool epoch_get(const BlockHash &, ApprovedEB &, MDB_txn *t=0);
    bool epoch_tip_put(const BlockHash &, MDB_txn*);
    bool epoch_tip_get(BlockHash &, MDB_txn *t=0);
    bool epoch_exists(const ApprovedEB &);
    bool epoch_exists(const BlockHash &, MDB_txn* t=0);

    //////////////////

    void checksum_put (MDB_txn *, uint64_t, uint8_t, logos::checksum const &);
    bool checksum_get (MDB_txn *, uint64_t, uint8_t, logos::checksum &);
    void checksum_del (MDB_txn *, uint64_t, uint8_t);

    // Return latest vote for an account from store
    std::shared_ptr<logos::vote> vote_get (MDB_txn *, logos::account const &);
    // Populate vote with the next sequence number
    std::shared_ptr<logos::vote> vote_generate (MDB_txn *, logos::account const &, logos::raw_key const &, std::shared_ptr<logos::block>);
    // Return either vote or the stored vote with a higher sequence number
    std::shared_ptr<logos::vote> vote_max (MDB_txn *, std::shared_ptr<logos::vote>);
    // Return latest vote for an account considering the vote cache
    std::shared_ptr<logos::vote> vote_current (MDB_txn *, logos::account const &);
    logos::store_iterator vote_begin (MDB_txn *);
    logos::store_iterator vote_end ();
    std::mutex cache_mutex;
    std::unordered_map<logos::account, std::shared_ptr<logos::vote>> vote_cache;

    void version_put (MDB_txn *, int);
    int version_get (MDB_txn *);

    void clear (MDB_dbi);

    logos::mdb_env environment;

    /**
     * Maps block hash to batch block
     * logos::block_hash -> BatchStateMessage
     */
    MDB_dbi batch_db;

    /**
     * Maps block hash to location in batch_blocks
     * where block is stored.
     * logos::block_hash -> location
     */
    MDB_dbi state_db;

    /**
     * Maps account to account information, head, rep, open, balance, timestamp and block count.
     * logos::account -> logos::block_hash, logos::block_hash, logos::block_hash, logos::amount, uint64_t, uint64_t
     */
    MDB_dbi account_db;

    /**
     * Maps account to reservation (transaction hash), reservation_epoch;.
     * logos::account -> logos::block_hash, uint32_t
     */
    MDB_dbi reservation_db;

    /**
     * Maps block hash to receive block.
     * logos::block_hash -> logos::state_block
     */
    MDB_dbi receive_db;

    /**
     * Maps delegate id to hash of most
     * recent batch block.
     * uint8_t -> logos::block_hash
     */
    MDB_dbi batch_tips_db;

    /**
     * Maps block hash to micro block
     * logos::block_hash -> version(1 byte), previous(logos::block_hash), merkle root(logos::block_hash), timestamp(8bytes), number batch blocks(2 bytes)
     * batch block tips [32] -> logos::block_hash, delegate number (1byte), epoch number (4bytes), micro block number (1 byte)
     */
    MDB_dbi micro_block_db;

    /**
     * Micro block tip
     * references micro block tip
     * 'microblocktip' -> logos::block_hash
     */
    MDB_dbi micro_block_tip_db;

    /**
   	 * Maps block hash to epoch
     * logos::block_hash -> 
     */
    MDB_dbi epoch_db;

  	/**
     * Epoch tip
     * references epoch tip
     * 'epochtip' -> logos::block_hash
     */
    MDB_dbi epoch_tip_db;

    /**
    * Token Accounts
    * block_hash token_id -> TokenAccount
    */
    MDB_dbi token_account_db;

	/**
	 * Maps head block to owning account
	 * logos::block_hash -> logos::account
	 */
    MDB_dbi frontiers;

    /**
     * Maps block hash to state block.
     * logos::block_hash -> logos::state_block
     */
    MDB_dbi state_blocks;

    /**
     * Maps (destination account, pending block) to (source account, amount).
     * logos::account, logos::block_hash -> logos::account, logos::amount
     */
    MDB_dbi pending;

    /**
     * Maps block hash to account and balance.
     * block_hash -> logos::account, logos::amount
     */
    MDB_dbi blocks_info;

    /**
     * Representative weights.
     * logos::account -> logos::uint128_t
     */
    MDB_dbi representation;

    /**
     * Unchecked bootstrap blocks.
     * logos::block_hash -> logos::block
     */
    MDB_dbi unchecked;

    /**
     * Mapping of region to checksum.
     * (uint56_t, uint8_t) -> logos::block_hash
     */
    MDB_dbi checksum;

    /**
     * Highest vote observed for account.
     * logos::account -> uint64_t
     */
    MDB_dbi vote;

    /**
     * Meta information about block store, such as versions.
     * logos::uint256_union (arbitrary key) -> blob
     */
    MDB_dbi meta;

    /**
     * P2p databases (peers, banlist).
     * std::string (database name) -> std::vector<uint8_t>
     */
    MDB_dbi p2p_db;

    Log log;
};
}
