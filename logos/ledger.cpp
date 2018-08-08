#include <logos/blockstore.hpp>
#include <logos/ledger.hpp>
#include <logos/node/common.hpp>
#include <logos/node/stats.hpp>

namespace
{

//CH I deleted roll_back stuff - we dont need it.

class ledger_processor : public logos::block_visitor
{
public:
	ledger_processor (logos::ledger &, MDB_txn *);
	virtual ~ledger_processor () = default;
	void state_block (logos::state_block const &) override;
	void state_block_impl (logos::state_block const &);
	logos::ledger & ledger;
	MDB_txn * transaction;
	logos::process_return result;
};

void ledger_processor::state_block (logos::state_block const & block_a)
{
	result.code = ledger.state_block_parsing_enabled (transaction) ? logos::process_result::progress : logos::process_result::state_block_disabled;
	if (result.code == logos::process_result::progress)
	{
		state_block_impl (block_a);
	}
}

void ledger_processor::state_block_impl (logos::state_block const & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, hash));
	result.code = existing ? logos::process_result::old : logos::process_result::progress; // Have we seen this block before? (Unambiguous)
	if (result.code == logos::process_result::progress)
	{
		result.code = validate_message (block_a.hashables.account, hash, block_a.signature) ? logos::process_result::bad_signature : logos::process_result::progress; // Is this block signed correctly (Unambiguous)
		if (result.code == logos::process_result::progress)
		{
			result.code = block_a.hashables.account.is_zero () ? logos::process_result::opened_burn_account : logos::process_result::progress; // Is this for the burn account? (Unambiguous)
			if (result.code == logos::process_result::progress)
			{
				logos::account_info info;
				result.amount = block_a.hashables.amount;
				auto is_send (false);
				auto account_error (ledger.store.account_get (transaction, block_a.hashables.account, info));
				if (!account_error)
				{
					// Account already exists
					result.code = block_a.hashables.previous.is_zero () ? logos::process_result::fork : logos::process_result::progress; // Has this account already been opened? (Ambigious)
					if (result.code == logos::process_result::progress)
					{
						result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? logos::process_result::progress : logos::process_result::gap_previous; // Does the previous block exist in the ledger? (Unambigious)
						if (result.code == logos::process_result::progress)
						{
							is_send = block_a.hashables.amount < info.balance;
							result.amount = is_send ? (info.balance.number () - result.amount.number ()) : (result.amount.number () - info.balance.number ());
							result.code = block_a.hashables.previous == info.head ? logos::process_result::progress : logos::process_result::fork; // Is the previous block the account's head block? (Ambigious)
						}
					}
				}
				else
				{
					// Account does not yet exists
					result.code = block_a.previous ().is_zero () ? logos::process_result::progress : logos::process_result::gap_previous; // Does the first block in an account yield 0 for previous() ? (Unambigious)
					if (result.code == logos::process_result::progress)
					{
						ledger.stats.inc (logos::stat::type::ledger, logos::stat::detail::open);
						result.code = !block_a.hashables.link.is_zero () ? logos::process_result::progress : logos::process_result::gap_source; // Is the first block receiving from a send ? (Unambigious)
					}
				}
				if (result.code == logos::process_result::progress)
				{
					if (!is_send)
					{
						if (!block_a.hashables.link.is_zero ())
						{
							result.code = ledger.store.block_exists (transaction, block_a.hashables.link) ? logos::process_result::progress : logos::process_result::gap_source; // Have we seen the source block already? (Harmless)
							if (result.code == logos::process_result::progress)
							{
								logos::pending_key key (block_a.hashables.account, block_a.hashables.link);
								logos::pending_info pending;
								result.code = ledger.store.pending_get (transaction, key, pending) ? logos::process_result::unreceivable : logos::process_result::progress; // Has this source already been received (Malformed)
								if (result.code == logos::process_result::progress)
								{
									result.code = result.amount == pending.amount ? logos::process_result::progress : logos::process_result::balance_mismatch;
								}
							}
						}
						else
						{
							// If there's no link, the balance must remain the same, only the representative can change
							result.code = result.amount.is_zero () ? logos::process_result::progress : logos::process_result::balance_mismatch;
						}
					}
				}
				if (result.code == logos::process_result::progress)
				{
					ledger.stats.inc (logos::stat::type::ledger, logos::stat::detail::state_block);
					result.state_is_send = is_send;
					ledger.store.block_put (transaction, hash, block_a);

					if (!info.rep_block.is_zero ())
					{
						// Move existing representation
						ledger.store.representation_add (transaction, info.rep_block, 0 - info.balance.number ());
					}
					// Add in amount delta
					ledger.store.representation_add (transaction, hash, block_a.hashables.amount.number ());

					if (is_send)
					{
						logos::pending_key key (block_a.hashables.link, hash);
						logos::pending_info info (block_a.hashables.account, result.amount.number ());
						ledger.store.pending_put (transaction, key, info);
						ledger.stats.inc (logos::stat::type::ledger, logos::stat::detail::send);
					}
					else if (!block_a.hashables.link.is_zero ())
					{
						ledger.store.pending_del (transaction, logos::pending_key (block_a.hashables.account, block_a.hashables.link));
						ledger.stats.inc (logos::stat::type::ledger, logos::stat::detail::receive);
					}

					ledger.change_latest (transaction, block_a.hashables.account, hash, hash, block_a.hashables.amount, info.block_count + 1, true);
					if (!ledger.store.frontier_get (transaction, info.head).is_zero ())
					{
						ledger.store.frontier_del (transaction, info.head);
					}
					// Frontier table is unnecessary for state blocks and this also prevents old blocks from being inserted on top of state blocks
					result.account = block_a.hashables.account;
				}
			}
		}
	}
}
ledger_processor::ledger_processor (logos::ledger & ledger_a, MDB_txn * transaction_a) :
ledger (ledger_a),
transaction (transaction_a)
{
}
} // namespace

size_t logos::shared_ptr_block_hash::operator() (std::shared_ptr<logos::block> const & block_a) const
{
	auto hash (block_a->hash ());
	auto result (static_cast<size_t> (hash.qwords[0]));
	return result;
}

bool logos::shared_ptr_block_hash::operator() (std::shared_ptr<logos::block> const & lhs, std::shared_ptr<logos::block> const & rhs) const
{
	return *lhs == *rhs;
}

logos::ledger::ledger (logos::block_store & store_a, logos::stat & stat_a, logos::block_hash const & state_block_parse_canary_a, logos::block_hash const & state_block_generate_canary_a) :
store (store_a),
stats (stat_a),
check_bootstrap_weights (true),
state_block_parse_canary (state_block_parse_canary_a),
state_block_generate_canary (state_block_generate_canary_a)
{
}

// Balance for account containing hash
logos::uint128_t logos::ledger::balance (MDB_txn * transaction_a, logos::block_hash const & hash_a)
{
	balance_visitor visitor (transaction_a, store);
	visitor.compute (hash_a);
	return visitor.result;
}

// Balance for an account by account number
logos::uint128_t logos::ledger::account_balance (MDB_txn * transaction_a, logos::account const & account_a)
{
	logos::uint128_t result (0);
	logos::account_info info;
	auto none (store.account_get (transaction_a, account_a, info));
	if (!none)
	{
		result = info.balance.number ();
	}
	return result;
}

logos::uint128_t logos::ledger::account_pending (MDB_txn * transaction_a, logos::account const & account_a)
{
	logos::uint128_t result (0);
	logos::account end (account_a.number () + 1);
	for (auto i (store.pending_begin (transaction_a, logos::pending_key (account_a, 0))), n (store.pending_begin (transaction_a, logos::pending_key (end, 0))); i != n; ++i)
	{
		logos::pending_info info (i->second);
		result += info.amount.number ();
	}
	return result;
}

logos::process_return logos::ledger::process (MDB_txn * transaction_a, logos::block const & block_a)
{
	ledger_processor processor (*this, transaction_a);
	block_a.visit (processor);
	return processor.result;
}

logos::block_hash logos::ledger::representative (MDB_txn * transaction_a, logos::block_hash const & hash_a)
{
	auto result (representative_calculated (transaction_a, hash_a));
	assert (result.is_zero () || store.block_exists (transaction_a, result));
	return result;
}

logos::block_hash logos::ledger::representative_calculated (MDB_txn * transaction_a, logos::block_hash const & hash_a)
{
	representative_visitor visitor (transaction_a, store);
	visitor.compute (hash_a);
	return visitor.result;
}

bool logos::ledger::block_exists (logos::block_hash const & hash_a)
{
	logos::transaction transaction (store.environment, nullptr, false);
	auto result (store.block_exists (transaction, hash_a));
	return result;
}

std::string logos::ledger::block_text (char const * hash_a)
{
	return block_text (logos::block_hash (hash_a));
}

std::string logos::ledger::block_text (logos::block_hash const & hash_a)
{
	std::string result;
	logos::transaction transaction (store.environment, nullptr, false);
	auto block (store.block_get (transaction, hash_a));
	if (block != nullptr)
	{
		block->serialize_json (result);
	}
	return result;
}

bool logos::ledger::is_send (MDB_txn * transaction_a, logos::state_block const & block_a)
{
	bool result (false);
	logos::block_hash previous (block_a.hashables.previous);
	if (!previous.is_zero ())
	{
		if (block_a.hashables.amount < balance (transaction_a, previous))
		{
			result = true;
		}
	}
	return result;
}

logos::block_hash logos::ledger::block_destination (MDB_txn * transaction_a, logos::block const & block_a)
{
	logos::block_hash result (0);

	logos::state_block const * state_block (dynamic_cast<logos::state_block const *> (&block_a));
	if (state_block != nullptr && is_send (transaction_a, *state_block))
	{
		result = state_block->hashables.link;
	}
	return result;
}

logos::block_hash logos::ledger::block_source (MDB_txn * transaction_a, logos::block const & block_a)
{
	// If block_a.source () is nonzero, then we have our source.
	// However, universal blocks will always return zero.
	logos::block_hash result (block_a.source ());
	logos::state_block const * state_block (dynamic_cast<logos::state_block const *> (&block_a));
	if (state_block != nullptr && !is_send (transaction_a, *state_block))
	{
		result = state_block->hashables.link;
	}
	return result;
}

// Vote weight of an account
logos::uint128_t logos::ledger::weight (MDB_txn * transaction_a, logos::account const & account_a)
{
	if (check_bootstrap_weights.load ())
	{
		auto blocks = store.block_count (transaction_a);
		if (blocks.sum () < bootstrap_weight_max_blocks)
		{
			auto weight = bootstrap_weights.find (account_a);
			if (weight != bootstrap_weights.end ())
			{
				return weight->second;
			}
		}
		else
		{
			check_bootstrap_weights = false;
		}
	}
	return store.representation_get (transaction_a, account_a);
}

// Return account containing hash
logos::account logos::ledger::account (MDB_txn * transaction_a, logos::block_hash const & hash_a)
{
	logos::account result;
	auto hash (hash_a);
	logos::block_hash successor (1);
	logos::block_info block_info;
	std::unique_ptr<logos::block> block (store.block_get (transaction_a, hash));
	while (!successor.is_zero () && block->type () != logos::block_type::state && store.block_info_get (transaction_a, successor, block_info))
	{
		successor = store.block_successor (transaction_a, hash);
		if (!successor.is_zero ())
		{
			hash = successor;
			block = store.block_get (transaction_a, hash);
		}
	}
	if (block->type () == logos::block_type::state)
	{
		auto state_block (dynamic_cast<logos::state_block *> (block.get ()));
		result = state_block->hashables.account;
	}
	else if (successor.is_zero ())
	{
		result = store.frontier_get (transaction_a, hash);
	}
	else
	{
		result = block_info.account;
	}
	assert (!result.is_zero ());
	return result;
}

// Return amount decrease or increase for block
logos::uint128_t logos::ledger::amount (MDB_txn * transaction_a, logos::block_hash const & hash_a)
{
	amount_visitor amount (transaction_a, store);
	amount.compute (hash_a);
	return amount.result;
}

// Return latest block for account
logos::block_hash logos::ledger::latest (MDB_txn * transaction_a, logos::account const & account_a)
{
	logos::account_info info;
	auto latest_error (store.account_get (transaction_a, account_a, info));
	return latest_error ? 0 : info.head;
}

// Return latest root for account, account number of there are no blocks for this account.
logos::block_hash logos::ledger::latest_root (MDB_txn * transaction_a, logos::account const & account_a)
{
	logos::account_info info;
	auto latest_error (store.account_get (transaction_a, account_a, info));
	logos::block_hash result;
	if (latest_error)
	{
		result = account_a;
	}
	else
	{
		result = info.head;
	}
	return result;
}

logos::checksum logos::ledger::checksum (MDB_txn * transaction_a, logos::account const & begin_a, logos::account const & end_a)
{
	logos::checksum result;
	auto error (store.checksum_get (transaction_a, 0, 0, result));
	assert (!error);
	return result;
}

void logos::ledger::dump_account_chain (logos::account const & account_a)
{
	logos::transaction transaction (store.environment, nullptr, false);
	auto hash (latest (transaction, account_a));
	while (!hash.is_zero ())
	{
		auto block (store.block_get (transaction, hash));
		assert (block != nullptr);
		std::cerr << hash.to_string () << std::endl;
		hash = block->previous ();
	}
}

bool logos::ledger::state_block_parsing_enabled (MDB_txn * transaction_a)
{
	return store.block_exists (transaction_a, state_block_parse_canary);
}

bool logos::ledger::state_block_generation_enabled (MDB_txn * transaction_a)
{
	return state_block_parsing_enabled (transaction_a) && store.block_exists (transaction_a, state_block_generate_canary);
}

void logos::ledger::checksum_update (MDB_txn * transaction_a, logos::block_hash const & hash_a)
{
	logos::checksum value;
	auto error (store.checksum_get (transaction_a, 0, 0, value));
	assert (!error);
	value ^= hash_a;
	store.checksum_put (transaction_a, 0, 0, value);
}

void logos::ledger::change_latest (MDB_txn * transaction_a, logos::account const & account_a, logos::block_hash const & hash_a, logos::block_hash const & rep_block_a, logos::amount const & balance_a, uint64_t block_count_a, bool is_state)
{
	logos::account_info info;
	auto exists (!store.account_get (transaction_a, account_a, info));
	if (exists)
	{
		checksum_update (transaction_a, info.head);
	}
	else
	{
		assert (store.block_get (transaction_a, hash_a)->previous ().is_zero ());
		info.open_block = hash_a;
	}
	if (!hash_a.is_zero ())
	{
		info.head = hash_a;
		info.rep_block = rep_block_a;
		info.balance = balance_a;
		info.modified = logos::seconds_since_epoch ();
		info.block_count = block_count_a;
		store.account_put (transaction_a, account_a, info);
		if (!(block_count_a % store.block_info_max) && !is_state)
		{
			logos::block_info block_info;
			block_info.account = account_a;
			block_info.balance = balance_a;
			store.block_info_put (transaction_a, hash_a, block_info);
		}
		checksum_update (transaction_a, hash_a);
	}
	else
	{
		store.account_del (transaction_a, account_a);
	}
}

std::unique_ptr<logos::block> logos::ledger::successor (MDB_txn * transaction_a, logos::uint256_union const & root_a)
{
	logos::block_hash successor (0);
	if (store.account_exists (transaction_a, root_a))
	{
		logos::account_info info;
		auto error (store.account_get (transaction_a, root_a, info));
		assert (!error);
		successor = info.open_block;
	}
	else
	{
		successor = store.block_successor (transaction_a, root_a);
	}
	std::unique_ptr<logos::block> result;
	if (!successor.is_zero ())
	{
		result = store.block_get (transaction_a, successor);
	}
	assert (successor.is_zero () || result != nullptr);
	return result;
}
