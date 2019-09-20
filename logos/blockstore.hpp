#pragma once

#include <logos/bootstrap/tips.hpp>
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
#include <logos/staking/staked_funds.hpp>
#include <logos/staking/thawing_funds.hpp>
#include <logos/staking/liability.hpp>
#include <logos/staking/voting_power.hpp>
#include <logos/rewards/epoch_rewards.hpp>

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
    int delete_current_record(unsigned int flags = 0);
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

    template<typename T> bool put(MDB_dbi&, const mdb_val &, const T &, MDB_txn *);
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
    uint32_t consensus_block_get_raw(const BlockHash & hash,
    		ConsensusType type,
    		uint32_t reserve,
			std::vector<uint8_t> & buf);

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
    void BatchBlocksIterator(const BatchTipHashes &start, const BatchTipHashes &end, IteratorBatchBlockReceiverCb cb);

    /// Iterates each delegates' batch state block chain. Traversing next pointer.
    /// Stop when the timestamp is greater than the cutoff.
    /// @param start tips to start iteration [in]
    /// @param cutoff timestamp to end iteration [in]
    /// @param cb function to call for each delegate's batch state block, the function's argument are
    ///   delegate id and BatchStateBlock
    void BatchBlocksIterator(const BatchTipHashes &start, const uint64_t &cutoff, IteratorBatchBlockReceiverCb cb);

    template<typename T>
    bool request_get(const BlockHash &hash, T & request, MDB_txn *transaction)
    {
        LOG_TRACE(log) << __func__ << " key " << hash.to_string();

        mdb_val val;
        if(mdb_get(transaction, request_db, mdb_val(hash), val))
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
    bool request_exists(const BlockHash & hash, MDB_txn * txn = nullptr);

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

    bool request_tip_put(uint8_t delegate_id, uint32_t epoch_number, const Tip &tip, MDB_txn *);
    bool request_tip_get(uint8_t delegate_id, uint32_t epoch_number, Tip & tip, MDB_txn *t=0);
    bool request_tip_del(uint8_t delegate_id, uint32_t epoch_number, MDB_txn *);
    bool request_block_update_prev(const BlockHash & hash, const BlockHash & prev, MDB_txn * transaction);

    // micro-block
    bool get(MDB_dbi &db, const mdb_val &key, mdb_val &value, MDB_txn *tx);
    bool micro_block_put(ApprovedMB const &, MDB_txn*);
    bool micro_block_get(const BlockHash &, ApprovedMB &, MDB_txn* t=0);
    bool micro_block_tip_put(const Tip &, MDB_txn*);
    bool micro_block_tip_get(Tip &, MDB_txn* t=0);
    bool micro_block_exists(const BlockHash &, MDB_txn* t=0);
    bool micro_block_exists(const ApprovedMB &);

    // epoch
    bool epoch_put(ApprovedEB const &, MDB_txn*);
    bool epoch_get(const BlockHash &, ApprovedEB &, MDB_txn *t=0);
    bool epoch_tip_put(const Tip &, MDB_txn*);
    bool epoch_tip_get(Tip &, MDB_txn *t=0);
    bool epoch_exists(const BlockHash &, MDB_txn* t=0);
    bool is_first_epoch();
    bool is_first_microblock();
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
        uint32_t epoch_number;      ///< epoch number for which the advertised address is valid
        uint8_t delegate_id;        ///< delegate who advertises its address
        uint8_t encr_delegate_id;   ///< delegate whose ecies public key is used to encrypt the address
    };
    struct ad_txa_key {
        uint32_t epoch_number;      ///< epoch number for which the advertised address is valid
        uint8_t delegate_id;        ///< delegate who advertises its address
    };

    template<typename KeyType>
    MDB_dbi get_ad_db()
    {
        return (std::is_same<KeyType, ad_key>::value) ? address_ad_db : address_ad_txa_db;
    }

    template<typename KeyType, typename ... Args>
    bool ad_put(MDB_txn* t, uint8_t *data, size_t size, Args ... args)
    {
        KeyType key{args ... };
        auto db = get_ad_db<KeyType>();
        auto status(mdb_put(t, db, mdb_val(sizeof(key), &key), mdb_val(size, data), 0));
        assert(status == 0);
        return status != 0;
    }

    template<typename KeyType, typename ... Args>
    bool ad_get(MDB_txn *t, std::vector<uint8_t> &data, Args ... args);

    template<typename KeyType, typename ... Args>
    void ad_del(MDB_txn *t, Args ... args)
    {
        KeyType key{args ... };
        auto db = get_ad_db<KeyType>();
        auto status (mdb_del (t, db, mdb_val(sizeof(key), &key), nullptr));

        assert (status == 0 || status == MDB_NOTFOUND);
    }

    bool rewards_exist(const mdb_val & key, MDB_txn * txn);
    bool global_rewards_exist(const mdb_val & key, MDB_txn * txn);

    bool rewards_put(const mdb_val & key, const RewardsInfo & info, MDB_txn * txn);
    bool global_rewards_put(const mdb_val & key, const GlobalRewardsInfo & info, MDB_txn * txn);

    bool rewards_get(const mdb_val & key, RewardsInfo & info, MDB_txn * txn);
    bool global_rewards_get(const mdb_val & key, GlobalRewardsInfo & info, MDB_txn * txn);

    bool rewards_remove(const mdb_val & key, MDB_txn * txn);
    bool global_rewards_remove(const mdb_val & key, MDB_txn * txn);

    bool fee_pool_get(const mdb_val & key, Amount & value, MDB_txn * txn = nullptr);
    bool fee_pool_put(const mdb_val & key, const Amount & value, MDB_txn * txn);
    bool fee_pool_remove(const mdb_val & key, MDB_txn * txn);

    bool stake_put(
            AccountAddress const & account,
            StakedFunds const & funds,
            MDB_txn* txn);

    bool stake_get(
            AccountAddress const & account,
            StakedFunds & funds,
            MDB_txn* txn);

    bool stake_del(
            AccountAddress const & account,
            MDB_txn* txn);

    bool thawing_put(
            AccountAddress const & account,
            ThawingFunds const & funds,
            MDB_txn* txn);

    bool thawing_del(
            AccountAddress const & account,
            ThawingFunds const & funds,
            MDB_txn* txn);

    bool liability_get(
            LiabilityHash const & hash,
            Liability & l,
            MDB_txn* txn);

    bool liability_put(LiabilityHash const & hash, Liability const & l, MDB_txn* txn);

    bool liability_update_amount(
            LiabilityHash const & hash,
            Amount const & amount,
            MDB_txn* txn);

    bool liability_exists(LiabilityHash const & hash, MDB_txn* txn);

    bool secondary_liability_put(
            AccountAddress const & source,
            LiabilityHash const & hash,
            MDB_txn* txn);

    bool secondary_liability_del(
            LiabilityHash const & hash,
            MDB_txn* txn);

    bool voting_power_get(
            AccountAddress const & rep,
            VotingPowerInfo& info,
            MDB_txn* txn);

    bool voting_power_put(
            AccountAddress const & rep,
            VotingPowerInfo const & info,
            MDB_txn* txn);

    bool voting_power_del(
            AccountAddress const & rep,
            MDB_txn* txn);

    bool fallback_voting_power_get(
            AccountAddress const & rep,
            VotingPowerFallback& f,
            MDB_txn* txn);

    bool fallback_voting_power_put(
            AccountAddress const & rep,
            VotingPowerFallback const & f,
            MDB_txn* txn);

    bool fallback_voting_power_del(
            AccountAddress const & rep,
            MDB_txn * txn);
    
    bool liability_del(LiabilityHash const & hash, MDB_txn* txn);

    /* @param db - db to iterate
     * @param start - key to start iteration on
     * @param operation - function to execute for each record in iteration
     * if operation returns false, iteration stops
     * @param txn - transaction (must be non-null)
     * @ returns true if error occurred, false otherwise
     * TODO Temprory abstraction, we still need to go through process to design an abstraction layer
     */
    template <typename T, typename R>
    bool iterate_db(
            MDB_dbi& db,
            T const & start,
            std::function<bool(R& record, logos::store_iterator&)> const & operation,
            MDB_txn* txn);

    //////////////////

    std::mutex cache_mutex;

    void version_put (MDB_txn *, int);
    int version_get (MDB_txn *);

    void clear (MDB_dbi, MDB_txn *t=0);

    // The lowest ranked candidate in leading_candidates_db. Kept up to date
    std::pair<AccountAddress, CandidateInfo> min_leading_candidate;

    // Number of candidates in leading_candidates_db
    size_t leading_candidates_size;

    logos::mdb_env environment;

    /**
     * Maps block hash to Request Block
     * logos::block_hash -> RequestBlock
     */
    MDB_dbi batch_db;

    /**
     * Maps block hash to location in Request Block
     * where block is stored.
     * logos::block_hash -> location
     */
    MDB_dbi request_db;

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

    /*
     * Rewards Info
     * logos::account || epoch_number -> RewardsInfo
     */
    MDB_dbi rewards_db;

    /**
     * Aggregate of Rewards Info
     * epoch_number -> GlobalRewardsInfo
     */
    MDB_dbi global_rewards_db;

    /**
     * Delegate transaction fee pool
     * epoch_number -> amount
     */
    MDB_dbi delegate_rewards_db;

    /**
     * Voting Power Info per epoch
     * logos::account -> VotingPowerInfo
     */
    MDB_dbi voting_power_db;

    /**
     * Voting Power for previous epoch
     * Used for certain race conditions
     * logos::account -> amount
     */
    MDB_dbi voting_power_fallback_db;

    /**
     * Staked funds per account (self stake and locked proxy)
     * logos::account -> StakedFunds
     */
    MDB_dbi staking_db;

    /**
     * Thawing funds per account
     * Uses duplicate keys
     * logos::account -> ThawingFunds
     */
    MDB_dbi thawing_db;

    /**
     * Liabilities
     * LiabilityHash -> Liability
     */
    MDB_dbi master_liabilities_db;

    /**
     * Liabilities where rep is a target
     * Key is rep account address
     * Uses duplicate keys
     * logos::account -> LiabilityHash
     */
    MDB_dbi rep_liabilities_db;

    /**
     * Secondary liabilities per account
     * Account is source of liability
     * Uses duplicate keys
     * logos::account -> LiabilityHash
     */
    MDB_dbi secondary_liabilities_db;

    Log log;
};

/**
 * Helper functions
 */

uint64_t get_request_tip_key(uint8_t delegate_id, uint32_t epoch_number);

}
