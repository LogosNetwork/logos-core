#pragma once

#include <logos/blockstore.hpp>
#include <logos/common.hpp>
#include <logos/node/common.hpp>
#include <logos/node/openclwork.hpp>

#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>

namespace logos
{
// The fan spreads a key out over the heap to decrease the likelihood of it being recovered by memory inspection
class fan
{
public:
	fan (logos::uint256_union const &, size_t);
	void value (logos::raw_key &);
	void value_set (logos::raw_key const &);
	std::vector<std::unique_ptr<logos::uint256_union>> values;

private:
	std::mutex mutex;
	void value_get (logos::raw_key &);
};
class wallet_value
{
public:
	wallet_value () = default;
	wallet_value (logos::mdb_val const &);
	wallet_value (logos::uint256_union const &, uint64_t);
	logos::mdb_val val () const;
	logos::private_key key;
	uint64_t work;
};
class node_config;
class kdf
{
public:
	void phs (logos::raw_key &, std::string const &, logos::uint256_union const &);
	std::mutex mutex;
};
enum class key_type
{
	not_a_type,
	unknown,
	adhoc,
	deterministic
};
class wallet_store
{
public:
	wallet_store (bool &, logos::kdf &, logos::transaction &, logos::account, unsigned, std::string const &);
	wallet_store (bool &, logos::kdf &, logos::transaction &, logos::account, unsigned, std::string const &, std::string const &);
	std::vector<logos::account> accounts (MDB_txn *);
	void initialize (MDB_txn *, bool &, std::string const &);
	logos::uint256_union check (MDB_txn *);
	bool rekey (MDB_txn *, std::string const &);
	bool valid_password (MDB_txn *);
	bool attempt_password (MDB_txn *, std::string const &);
	void wallet_key (logos::raw_key &, MDB_txn *);
	void seed (logos::raw_key &, MDB_txn *);
	void seed_set (MDB_txn *, logos::raw_key const &);
	logos::key_type key_type (logos::wallet_value const &);
	logos::public_key deterministic_insert (MDB_txn *);
	void deterministic_key (logos::raw_key &, MDB_txn *, uint32_t);
	uint32_t deterministic_index_get (MDB_txn *);
	void deterministic_index_set (MDB_txn *, uint32_t);
	void deterministic_clear (MDB_txn *);
	logos::uint256_union salt (MDB_txn *);
	bool is_representative (MDB_txn *);
	logos::account representative (MDB_txn *);
	void representative_set (MDB_txn *, logos::account const &);
	logos::public_key insert_adhoc (MDB_txn *, logos::raw_key const &);
	void insert_watch (MDB_txn *, logos::public_key const &);
	void erase (MDB_txn *, logos::public_key const &);
	logos::wallet_value entry_get_raw (MDB_txn *, logos::public_key const &);
	void entry_put_raw (MDB_txn *, logos::public_key const &, logos::wallet_value const &);
	bool fetch (MDB_txn *, logos::public_key const &, logos::raw_key &);
	bool exists (MDB_txn *, logos::public_key const &);
	void destroy (MDB_txn *);
	logos::store_iterator find (MDB_txn *, logos::uint256_union const &);
	logos::store_iterator begin (MDB_txn *, logos::uint256_union const &);
	logos::store_iterator begin (MDB_txn *);
	logos::store_iterator end ();
	void derive_key (logos::raw_key &, MDB_txn *, std::string const &);
	void serialize_json (MDB_txn *, std::string &);
	void write_backup (MDB_txn *, boost::filesystem::path const &);
	bool move (MDB_txn *, logos::wallet_store &, std::vector<logos::public_key> const &);
	bool import (MDB_txn *, logos::wallet_store &);
	bool work_get (MDB_txn *, logos::public_key const &, uint64_t &);
	void work_put (MDB_txn *, logos::public_key const &, uint64_t);
	unsigned version (MDB_txn *);
	void version_put (MDB_txn *, unsigned);
	void upgrade_v1_v2 ();
	void upgrade_v2_v3 ();
	logos::fan password;
	logos::fan wallet_key_mem;
	static unsigned const version_1;
	static unsigned const version_2;
	static unsigned const version_3;
	static unsigned const version_current;
	static logos::uint256_union const version_special;
	static logos::uint256_union const wallet_key_special;
	static logos::uint256_union const salt_special;
	static logos::uint256_union const check_special;
	static logos::uint256_union const representative_special;
	static logos::uint256_union const seed_special;
	static logos::uint256_union const deterministic_index_special;
	static int const special_count;
	static unsigned const kdf_full_work = 64 * 1024;
	static unsigned const kdf_test_work = 8;
	static unsigned const kdf_work = logos::logos_network ==logos::logos_networks::logos_test_network ? kdf_test_work : kdf_full_work;
	logos::kdf & kdf;
	logos::mdb_env & environment;
	MDB_dbi handle;
	std::recursive_mutex mutex;
};
class node;
// A wallet is a set of account keys encrypted by a common encryption key
class wallet : public std::enable_shared_from_this<logos::wallet>
{
public:
	std::shared_ptr<logos::block> change_action (logos::account const &, logos::account const &, bool = true);
	std::shared_ptr<logos::block> receive_action (logos::block const &, logos::account const &, logos::uint128_union const &, bool = true);
	std::shared_ptr<logos::block> send_action (logos::account const &, logos::account const &, logos::uint128_t const &, bool = true, boost::optional<std::string> = {});
	wallet (bool &, logos::transaction &, logos::node &, std::string const &);
	wallet (bool &, logos::transaction &, logos::node &, std::string const &, std::string const &);
	void enter_initial_password ();
	bool valid_password ();
	bool enter_password (std::string const &);
	logos::public_key insert_adhoc (logos::raw_key const &, bool = true);
	logos::public_key insert_adhoc (MDB_txn *, logos::raw_key const &, bool = true);
	void insert_watch (MDB_txn *, logos::public_key const &);
	logos::public_key deterministic_insert (MDB_txn *, bool = true);
	logos::public_key deterministic_insert (bool = true);
	bool exists (logos::public_key const &);
	bool import (std::string const &, std::string const &);
	void serialize (std::string &);
	bool change_sync (logos::account const &, logos::account const &);
	void change_async (logos::account const &, logos::account const &, std::function<void(std::shared_ptr<logos::block>)> const &, bool = true);
	bool receive_sync (std::shared_ptr<logos::block>, logos::account const &, logos::uint128_t const &);
	void receive_async (std::shared_ptr<logos::block>, logos::account const &, logos::uint128_t const &, std::function<void(std::shared_ptr<logos::block>)> const &, bool = true);
	logos::block_hash send_sync (logos::account const &, logos::account const &, logos::uint128_t const &);
	void send_async (logos::account const &, logos::account const &, logos::uint128_t const &, std::function<void(std::shared_ptr<logos::block>)> const &, bool = true, boost::optional<std::string> = {});
	void work_apply (logos::account const &, std::function<void(uint64_t)>);
	void work_cache_blocking (logos::account const &, logos::block_hash const &);
	void work_update (MDB_txn *, logos::account const &, logos::block_hash const &, uint64_t);
	void work_ensure (logos::account const &, logos::block_hash const &);
	bool search_pending ();
	void init_free_accounts (MDB_txn *);
	bool should_generate_state_block (MDB_txn *, logos::block_hash const &);
	/** Changes the wallet seed and returns the first account */
	logos::public_key change_seed (MDB_txn * transaction_a, logos::raw_key const & prv_a);
	std::unordered_set<logos::account> free_accounts;
	std::function<void(bool, bool)> lock_observer;
	logos::wallet_store store;
	logos::node & node;
};
// The wallets set is all the wallets a node controls.  A node may contain multiple wallets independently encrypted and operated.
class wallets
{
public:
	wallets (bool &, logos::node &);
	~wallets ();
	std::shared_ptr<logos::wallet> open (logos::uint256_union const &);
	std::shared_ptr<logos::wallet> create (logos::uint256_union const &);
	bool search_pending (logos::uint256_union const &);
	void search_pending_all ();
	void destroy (logos::uint256_union const &);
	void do_wallet_actions ();
	void queue_wallet_action (logos::uint128_t const &, std::function<void()> const &);
	void foreach_representative (MDB_txn *, std::function<void(logos::public_key const &, logos::raw_key const &)> const &);
	bool exists (MDB_txn *, logos::public_key const &);
	void stop ();
	std::function<void(bool)> observer;
	std::unordered_map<logos::uint256_union, std::shared_ptr<logos::wallet>> items;
	std::multimap<logos::uint128_t, std::function<void()>, std::greater<logos::uint128_t>> actions;
	std::mutex mutex;
	std::condition_variable condition;
	logos::kdf kdf;
	MDB_dbi handle;
	MDB_dbi send_action_ids;
	logos::node & node;
	bool stopped;
	std::thread thread;
	static logos::uint128_t const generate_priority;
	static logos::uint128_t const high_priority;
};
}
