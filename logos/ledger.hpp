#pragma once

#include <logos/common.hpp>

namespace logos
{
class block_store;
class stat;

class shared_ptr_block_hash
{
public:
    size_t operator() (std::shared_ptr<logos::block> const &) const;
    bool operator() (std::shared_ptr<logos::block> const &, std::shared_ptr<logos::block> const &) const;
};
using tally_t = std::map<logos::uint128_t, std::shared_ptr<logos::block>, std::greater<logos::uint128_t>>;
class ledger
{
public:
    ledger (logos::block_store &, logos::stat &, logos::block_hash const & = 0, logos::block_hash const & = 0);
    //CH std::pair<logos::uint128_t, std::shared_ptr<logos::block>> winner (MDB_txn *, logos::votes const & votes_a);
    // Map of weight -> associated block, ordered greatest to least
    logos::tally_t tally (MDB_txn *, logos::votes const &);
    logos::account account (MDB_txn *, logos::block_hash const &);
    logos::uint128_t amount (MDB_txn *, logos::block_hash const &);
    logos::uint128_t balance (MDB_txn *, logos::block_hash const &);
    logos::uint128_t account_balance (MDB_txn *, logos::account const &);
    logos::uint128_t account_pending (MDB_txn *, logos::account const &);
    logos::uint128_t weight (MDB_txn *, logos::account const &);
    std::unique_ptr<logos::block> successor (MDB_txn *, logos::block_hash const &);
    //CH std::unique_ptr<logos::block> forked_block (MDB_txn *, logos::block const &);
    logos::block_hash latest (MDB_txn *, logos::account const &);
    logos::block_hash latest_root (MDB_txn *, logos::account const &);
    logos::block_hash representative (MDB_txn *, logos::block_hash const &);
    logos::block_hash representative_calculated (MDB_txn *, logos::block_hash const &);
    bool block_exists (logos::block_hash const &);
    std::string block_text (char const *);
    std::string block_text (logos::block_hash const &);
    bool is_send (MDB_txn *, logos::state_block const &);
    logos::block_hash block_destination (MDB_txn *, logos::block const &);
    logos::block_hash block_source (MDB_txn *, logos::block const &);
    logos::process_return process (MDB_txn *, logos::block const &);
    //CH void rollback (MDB_txn *, logos::block_hash const &);
    void change_latest (MDB_txn *, logos::account const &, logos::block_hash const &, logos::account const &, logos::uint128_union const &, uint64_t, bool = false);
    void checksum_update (MDB_txn *, logos::block_hash const &);
    logos::checksum checksum (MDB_txn *, logos::account const &, logos::account const &);
    void dump_account_chain (logos::account const &);
    bool state_block_parsing_enabled (MDB_txn *);
    bool state_block_generation_enabled (MDB_txn *);
    static logos::uint128_t const unit;
    logos::block_store & store;
    logos::stat & stats;
    std::unordered_map<logos::account, logos::uint128_t> bootstrap_weights;
    uint64_t bootstrap_weight_max_blocks;
    std::atomic<bool> check_bootstrap_weights;
    logos::block_hash state_block_parse_canary;
    logos::block_hash state_block_generate_canary;
};
};
