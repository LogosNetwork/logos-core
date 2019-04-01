#pragma once

#include <logos/lib/numbers.hpp>
#include <logos/lib/utility.hpp>

#include <assert.h>
#include <blake2/blake2.h>
#include <boost/property_tree/json_parser.hpp>
#include <streambuf>

namespace logos
{

std::string to_string_hex (uint64_t);
bool from_string_hex (std::string const &, uint64_t &);

class block_visitor;
enum class block_type : uint8_t
{
    invalid = 0,
    not_a_block = 1,
    send = 2,
    receive = 3,
    open = 4,
    change = 5,
    state = 6,
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
    state_hashables();

    state_hashables (logos::account const & account,
                     logos::block_hash const & previous,
                     logos::account const & representative,
                     logos::amount const & amount,
                     logos::amount const & transaction_fee,
                     logos::uint256_union const & link);

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
    logos::amount transaction_fee;
    // Link field contains source block_hash if receiving, destination account if sending
    logos::uint256_union link;
};
class state_block : public logos::block
{
public:
    state_block() = default;

    state_block (logos::account const & account,
                 logos::block_hash const & previous,
                 logos::account const & representative,
                 logos::amount const & amount,
                 logos::amount const & transaction_fee,
                 logos::uint256_union const & link,
                 logos::raw_key const & prv,
                 logos::public_key const & pub,
                 uint64_t work,
                 uint64_t timestamp = 0);

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
    boost::property_tree::ptree serialize_json () const;
    bool deserialize (logos::stream &);
    bool deserialize_json (boost::property_tree::ptree const &);
    void visit (logos::block_visitor &) const override;
    logos::block_type type () const override;
    logos::signature block_signature () const override;
    void signature_set (logos::uint512_union const &) override;
    bool operator== (logos::block const &) const override;
    bool operator== (logos::state_block const &) const;
    bool valid_predecessor (logos::block const &) const override;

    static size_t constexpr size = sizeof (logos::account) +
                                   sizeof (logos::block_hash) +
                                   sizeof (logos::account) +
                                   sizeof (logos::amount) +
                                   sizeof (logos::amount) +
                                   sizeof (logos::uint256_union) +
                                   sizeof (logos::signature) +
                                   sizeof (uint64_t) +
                                   sizeof (uint64_t);

    logos::state_hashables hashables;
    logos::signature signature;
    uint64_t work;
    uint64_t timestamp = 0;
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
