#pragma once

#include <logos/lib/numbers.hpp>

#include <assert.h>
#include <blake2/blake2.h>
#include <boost/property_tree/json_parser.hpp>
#include <streambuf>

namespace logos
{
std::string to_string_hex (uint64_t);
bool from_string_hex (std::string const &, uint64_t &);
// We operate on streams of uint8_t by convention
using stream = std::basic_streambuf<uint8_t>;
// Read a raw byte stream the size of `T' and fill value.
template <typename T>
bool read (logos::stream & stream_a, T & value)
{
	static_assert (std::is_pod<T>::value, "Can't stream read non-standard layout types");
	auto amount_read (stream_a.sgetn (reinterpret_cast<uint8_t *> (&value), sizeof (value)));
	return amount_read != sizeof (value);
}
template <typename T>
void write (logos::stream & stream_a, T const & value)
{
	static_assert (std::is_pod<T>::value, "Can't stream write non-standard layout types");
	auto amount_written (stream_a.sputn (reinterpret_cast<uint8_t const *> (&value), sizeof (value)));
	assert (amount_written == sizeof (value));
}
class block_visitor;
enum class block_type : uint8_t
{
	invalid = 0,
	not_a_block = 1,
	send = 2,
	receive = 3,
	open = 4,
	change = 5,
	state = 6
};
class block
{
public:
	// Return a digest of the hashables in this block.
	logos::block_hash hash () const;
	std::string to_json ();
	virtual void hash (blake2b_state &) const = 0;
	virtual uint64_t block_work () const = 0;
	virtual void block_work_set (uint64_t) = 0;
	// Previous block in account's chain, zero for open block
	virtual logos::block_hash previous () const = 0;
	// Source block for open/receive blocks, zero otherwise.
	virtual logos::block_hash source () const = 0;
	// Previous block or account number for open blocks
	virtual logos::block_hash root () const = 0;
	virtual logos::account representative () const = 0;
	virtual void serialize (logos::stream &) const = 0;
	virtual void serialize_json (std::string &) const = 0;
	virtual void visit (logos::block_visitor &) const = 0;
	virtual bool operator== (logos::block const &) const = 0;
	virtual logos::block_type type () const = 0;
	virtual logos::signature block_signature () const = 0;
	virtual void signature_set (logos::uint512_union const &) = 0;
	virtual ~block () = default;
	virtual bool valid_predecessor (logos::block const &) const = 0;
};
class state_hashables
{
public:
    state_hashables() = default;
	state_hashables (logos::account const &, logos::block_hash const &, logos::account const &, logos::amount const &, logos::uint256_union const &);
	state_hashables (bool &, logos::stream &);
	state_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	// Account# / public key that operates this account
	// Uses:
	// Bulk signature validation in advance of further ledger processing
	// Arranging uncomitted transactions by account
	logos::account account;
	// Previous transaction in this chain
	logos::block_hash previous;
	// Representative of this account
	logos::account representative;
	// Current balance of this account
	// Allows lookup of account balance simply by looking at the head block
	logos::amount amount;
	// Link field contains source block_hash if receiving, destination account if sending
	logos::uint256_union link;
};
class state_block : public logos::block
{
public:
    state_block() = default;
	state_block (logos::account const &, logos::block_hash const &, logos::account const &, logos::amount const &, logos::uint256_union const &, logos::raw_key const &, logos::public_key const &, uint64_t);
	state_block (bool &, logos::stream &);
	state_block (bool &, boost::property_tree::ptree const &);
	virtual ~state_block () = default;
	using logos::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	logos::block_hash previous () const override;
	logos::block_hash source () const override;
	logos::block_hash root () const override;
	logos::account representative () const override;
	void serialize (logos::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (logos::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (logos::block_visitor &) const override;
	logos::block_type type () const override;
	logos::signature block_signature () const override;
	void signature_set (logos::uint512_union const &) override;
	bool operator== (logos::block const &) const override;
	bool operator== (logos::state_block const &) const;
	bool valid_predecessor (logos::block const &) const override;
	static size_t constexpr size = sizeof (logos::account) + sizeof (logos::block_hash) + sizeof (logos::account) + sizeof (logos::amount) + sizeof (logos::uint256_union) + sizeof (logos::signature) + sizeof (uint64_t);
	logos::state_hashables hashables;
	logos::signature signature;
	uint64_t work;
};
class block_visitor
{
public:
	virtual void state_block (logos::state_block const &) = 0;
	virtual ~block_visitor () = default;
};
std::unique_ptr<logos::block> deserialize_block (logos::stream &);
std::unique_ptr<logos::block> deserialize_block (logos::stream &, logos::block_type);
std::unique_ptr<logos::block> deserialize_block_json (boost::property_tree::ptree const &);
void serialize_block (logos::stream &, logos::block const &);
}

struct CompressedStateBlock
{
    using Storage256 = std::array<uint8_t, 32>;
    using Storage128 = std::array<uint8_t, 16>;

    CompressedStateBlock(const logos::state_block & block)
        : account       (block.hashables.account.bytes)
        , previous      (block.hashables.previous.bytes)
        , representative(block.hashables.representative.bytes)
        , balance       (block.hashables.amount.bytes)
        , link          (block.hashables.link.bytes)
    {}

    CompressedStateBlock() = default;

    void Hash(blake2b_state & hash) const
    {
        blake2b_update(&hash, account.data(), sizeof(account));
        blake2b_update(&hash, previous.data(), sizeof(previous));
        blake2b_update(&hash, representative.data(), sizeof(representative));
        blake2b_update(&hash, balance.data(), sizeof(balance));
        blake2b_update(&hash, link.data(), sizeof(link));
    }

    Storage256 account;
    Storage256 previous;
    Storage256 representative;
    Storage128 balance;
    Storage256 link;
} __attribute__((packed));
