#pragma once

#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/messages/common.hpp>
#include <logos/microblock/microblock.hpp>
#include <logos/request/utility.hpp>
#include <logos/token/account.hpp>
#include <logos/epoch/epoch.hpp>
#include <logos/lib/log.hpp>
#include <logos/common.hpp>
#include <logos/elections/candidate.hpp>
#include <logos/elections/representative.hpp>

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
    using IteratorBatchBlockReceiverCb = std::function<void(uint8_t, const ApprovedRB &)>;

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
    bool del(MDB_dbi&, const mdb_val &, MDB_txn * tx);
    bool del(MDB_dbi& db, const Byte32Array &key_32b, MDB_txn *tx)
    {
        mdb_val key(key_32b);
        return del(db, key, tx);
    }

    //////////////////

    // abstract away consensus types
    bool consensus_block_get (const BlockHash & hash, ApprovedRB & block);
    bool consensus_block_get (const BlockHash & hash, ApprovedMB & block);
    bool consensus_block_get (const BlockHash & hash, ApprovedEB & block);
    // return true if cannot found hash
    bool consensus_block_update_next(const BlockHash & hash, const BlockHash & next, ConsensusType type, MDB_txn * transaction);

    bool request_block_exists(const ApprovedRB & block);
    bool request_block_put(ApprovedRB const & block, MDB_txn * transaction);
    bool request_block_put(ApprovedRB const & block, const BlockHash & hash, MDB_txn *transaction);
    bool request_block_get(const BlockHash & hash, ApprovedRB & block);
    bool request_block_get(const BlockHash &hash, ApprovedRB &block, MDB_txn *);


    /// Iterates each delegates' batch state block chain. Traversing previous pointer.
    /// Stop when reached the end tips.
    /// @param start tips to start iteration [in]
    /// @param end tips to end iteration [in]
    /// @param cb function to call for each delegate's batch state block, the function's argument are
    ///   delegate id and BatchStateBlock
    void BatchBlocksIterator(const BatchTips &start, const BatchTips &end, IteratorBatchBlockReceiverCb cb);

    /// Iterates each delegates' batch state block chain. Traversing next pointer.
    /// Stop when the timestamp is greater than the cutoff.
    /// @param start tips to start iteration [in]
    /// @param cutoff timestamp to end iteration [in]
    /// @param cb function to call for each delegate's batch state block, the function's argument are
    ///   delegate id and BatchStateBlock
    void BatchBlocksIterator(const BatchTips &start, const uint64_t &cutoff, IteratorBatchBlockReceiverCb cb);

    template<typename T>
    bool request_get(const BlockHash &hash, T & request, MDB_txn *transaction)
    {
        LOG_TRACE(log) << __func__ << " key " << hash.to_string();

        mdb_val val;
        if(mdb_get(transaction, state_db, mdb_val(hash), val))
        {
            LOG_TRACE(log) << __func__ << " mdb_get failed";
            return true;
        }

        bool error = false;
        new (&request) T(error, val);

        assert(GetRequestType<T>() == request.type);
        assert(!error);

        return error;
    }

    bool request_get(const BlockHash &hash, std::shared_ptr<Request> & request, MDB_txn *transaction);
    bool request_put(const Request &, MDB_txn *);
    bool request_exists(const Request & request);
    bool request_exists(const BlockHash & hash);

    bool token_user_status_get(const BlockHash & token_user_id, TokenUserStatus & status, MDB_txn* t=0);
    bool token_user_status_put(const BlockHash & token_user_id, const TokenUserStatus & status, MDB_txn *);
    bool token_user_status_del(const BlockHash & token_user_id, MDB_txn *);

    bool token_account_get(const BlockHash & token_id, TokenAccount & info, MDB_txn* t=0);
    bool token_account_put (const BlockHash &, TokenAccount const &, MDB_txn *);

    bool account_get(AccountAddress const & account_a, std::shared_ptr<Account> & info_a, MDB_txn* t=0);
    bool account_get(AccountAddress const & account_a, account_info & info_a, MDB_txn* t=0);
    bool account_db_empty();
    bool account_put (AccountAddress const &, std::shared_ptr<Account> info, MDB_txn *);
    bool account_put (AccountAddress const &, logos::account_info const &, MDB_txn *);
    bool account_exists (AccountAddress const &);

    void reservation_put(AccountAddress const & account_a, logos::reservation_info const & info_a, MDB_txn *);
    bool reservation_get(AccountAddress const & account_a, logos::reservation_info & info_a, MDB_txn * t=nullptr);
    void reservation_del(AccountAddress const & account_a, MDB_txn *);

    bool receive_put(const BlockHash & hash, const ReceiveBlock & block, MDB_txn *);
    bool receive_get(const BlockHash & hash, ReceiveBlock & block, MDB_txn *);
    bool receive_exists(const BlockHash & hash);

    bool request_tip_put(uint8_t delegate_id, uint32_t epoch_number, const BlockHash &hash, MDB_txn *);
    bool request_tip_get(uint8_t delegate_id, uint32_t epoch_number, BlockHash &hash);
    bool request_tip_del(uint8_t delegate_id, uint32_t epoch_number, MDB_txn *);
    bool request_block_update_prev(const BlockHash & hash, const BlockHash & prev, MDB_txn * transaction);

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
    bool epoch_exists(const BlockHash &, MDB_txn* t=0);
    bool is_first_epoch();
    uint32_t epoch_number_stored();
    /// Get each delegate's first request block in an epoch, only used when linking two request tips
    /// @param epoch number to retrieve [in]
    /// @param list of delegate request block hash to populate [in]
    void GetEpochFirstRBs(uint32_t epoch_number, BatchTips & epoch_firsts);

    bool epoch_exists(const ApprovedEB & block);
    bool epoch_get_n(
            uint32_t ago,
            ApprovedEB &,
            MDB_txn *t=0,
            const std::function<bool(ApprovedEB&)>& filter=[](ApprovedEB&)->bool{return true;});

    bool rep_get(
            AccountAddress const & account,
            RepInfo & rep_info,
            MDB_txn* t=0);
    bool rep_put(
            AccountAddress const & account,
            const RepInfo & rep_info,
            MDB_txn *);

    bool rep_mark_remove(
            const AccountAddress & account,
            MDB_txn *);

    bool candidate_get(
            const AccountAddress & account,
            CandidateInfo & candidate_info,
            MDB_txn* t=0);
    bool candidate_put(
            const AccountAddress & account,
            const CandidateInfo & candidate_info,
            MDB_txn *);

    bool candidate_add_vote(
            const AccountAddress & account,
            Amount weighted_vote,
            uint32_t cur_epoch_num,
            MDB_txn *);

    bool candidate_mark_remove(
            const AccountAddress & account,
            MDB_txn *);

    bool update_leading_candidates(
            const AccountAddress & account,
            const CandidateInfo & candidate_info,
            MDB_txn* txn);

    //updates min_leading_candidate and leading_candidates_size members
    //required on startup (in case of crash), and whenever leading_candidates_db
    //is updated
    void sync_leading_candidates(MDB_txn* txn);

    bool candidate_is_greater(
            const AccountAddress& account1,
            const CandidateInfo& candidate1,
            const AccountAddress& account2,
            const CandidateInfo& candidate2);

    // Address advertisement
    struct ad_key {
        uint32_t epoch_number;
        uint8_t delegate_id;
        uint8_t encr_delegate_id;
    };
    struct ad_txa_key {
        uint32_t epoch_number;
        uint8_t delegate_id;
    };

    template<typename KeyType>
    MDB_dbi get_ad_db()
    {
        return (std::is_same<KeyType, ad_key>::value) ? address_ad_db : address_ad_txa_db;
    }

    template<typename KeyType, typename ... Args>
    void ad_put(MDB_txn* t, uint8_t *data, size_t size, Args ... args)
    {
        KeyType key{args ... };
        auto db = get_ad_db<KeyType>();
        auto status(mdb_put(t, db, mdb_val(sizeof(key), &key), mdb_val(size, data), 0));
        assert(status == 0);
    }

    template<typename KeyType, typename ... Args>
    bool ad_get(MDB_txn *t, std::vector<uint8_t> &data, Args ... args)
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

    template<typename KeyType, typename ... Args>
    void ad_del(MDB_txn *t, Args ... args)
    {
        KeyType key{args ... };
        auto db = get_ad_db<KeyType>();
        auto status (mdb_del (t, db, mdb_val(sizeof(key), &key), nullptr));

        assert (status == 0 || status == MDB_NOTFOUND);
    }

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

    void clear (MDB_dbi, MDB_txn *t=0);

    // The lowest ranked candidate in leading_candidates_db. Kept up to date
    std::pair<AccountAddress, CandidateInfo> min_leading_candidate;

    // Number of candidates in leading_candidates_db
    size_t leading_candidates_size;

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
     * Maps (delegate id, epoch number) combination to hash of most
     * recent request block.
     * (uint8_t + uint32_t) -> logos::block_hash
     */
    MDB_dbi request_tips_db;

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
    * Token User Statuses
    * (Untethered accounts only)
    * block_hash token_user_id -> TokenUserStatus
    */
    MDB_dbi token_user_status_db;

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
     * Representative info
     * logos::account -> logos::RepInfo
     */
    MDB_dbi representative_db;

    /**
     * Candidacy info
     * AccountAddress -> CandidateInfo
     */
    MDB_dbi candidacy_db;

    /**
     * Candidacy info of candidates who are currently winning election
     * AccountAddress -> CandidateInfo 
     */
    MDB_dbi leading_candidates_db;

    /**
     * AccountAddresses of candidates to be deleted at epoch transition
     * 0 -> AccountAddress
     * Note, this database uses duplicate keys, where every entry has a key
     * of 0. 
     */
    MDB_dbi remove_candidates_db;

    /**
     * AccountAddresses of representatives to be deleted at epoch transition
     * 0 -> AccountAddress
     * Note, this database uses duplicate keys, where every entry has a key
     * of 0. 
     */
    MDB_dbi remove_reps_db;

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

    /**
     * AddressAd database
     * epoch_number, delegate_id, encr_delegate_id -> std::vector<uint8_t>
     */
    MDB_dbi address_ad_db;

    /**
     * AddressAdTxAcceptor database
     * epoch_number, delegate_id -> std::vector<uint8_t>
     */
    MDB_dbi address_ad_txa_db;

    Log log;
};

/**
 * Helper functions
 */

logos::mdb_val get_request_tip_key(uint8_t delegate_id, uint32_t epoch_number);

}
