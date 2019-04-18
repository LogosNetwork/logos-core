#include <logos/versioning.hpp>

logos::account_info_v1::account_info_v1 () :
head (0),
staking_subchain_head (0),
balance (0),
modified (0)
{
}

logos::account_info_v1::account_info_v1 (MDB_val const & val_a)
{
    assert (val_a.mv_size == sizeof (*this));
    static_assert (sizeof (head) + sizeof (staking_subchain_head) + sizeof (balance) + sizeof (modified) == sizeof (*this), "Class not packed");
    std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

logos::account_info_v1::account_info_v1 (logos::block_hash const & head_a, logos::block_hash const & staking_subchain_head_a, logos::amount const & balance_a, uint64_t modified_a) :
head (head_a),
staking_subchain_head (staking_subchain_head_a),
balance (balance_a),
modified (modified_a)
{
}

void logos::account_info_v1::serialize (logos::stream & stream_a) const
{
    write (stream_a, head.bytes);
    write (stream_a, staking_subchain_head.bytes);
    write (stream_a, balance.bytes);
    write (stream_a, modified);
}

bool logos::account_info_v1::deserialize (logos::stream & stream_a)
{
    auto error (read (stream_a, head.bytes));
    if (!error)
    {
        error = read (stream_a, staking_subchain_head.bytes);
        if (!error)
        {
            error = read (stream_a, balance.bytes);
            if (!error)
            {
                error = read (stream_a, modified);
            }
        }
    }
    return error;
}

logos::mdb_val logos::account_info_v1::val () const
{
    return logos::mdb_val (sizeof (*this), const_cast<logos::account_info_v1 *> (this));
}

logos::pending_info_v3::pending_info_v3 () :
source (0),
amount (0),
destination (0)
{
}

logos::pending_info_v3::pending_info_v3 (MDB_val const & val_a)
{
    assert (val_a.mv_size == sizeof (*this));
    static_assert (sizeof (source) + sizeof (amount) + sizeof (destination) == sizeof (*this), "Packed class");
    std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

logos::pending_info_v3::pending_info_v3 (logos::account const & source_a, logos::amount const & amount_a, logos::account const & destination_a) :
source (source_a),
amount (amount_a),
destination (destination_a)
{
}

void logos::pending_info_v3::serialize (logos::stream & stream_a) const
{
    logos::write (stream_a, source.bytes);
    logos::write (stream_a, amount.bytes);
    logos::write (stream_a, destination.bytes);
}

bool logos::pending_info_v3::deserialize (logos::stream & stream_a)
{
    auto error (logos::read (stream_a, source.bytes));
    if (!error)
    {
        error = logos::read (stream_a, amount.bytes);
        if (!error)
        {
            error = logos::read (stream_a, destination.bytes);
        }
    }
    return error;
}

bool logos::pending_info_v3::operator== (logos::pending_info_v3 const & other_a) const
{
    return source == other_a.source && amount == other_a.amount && destination == other_a.destination;
}

logos::mdb_val logos::pending_info_v3::val () const
{
    return logos::mdb_val (sizeof (*this), const_cast<logos::pending_info_v3 *> (this));
}

logos::account_info_v5::account_info_v5 () :
head (0),
staking_subchain_head (0),
open_block (0),
balance (0),
modified (0)
{
}

logos::account_info_v5::account_info_v5 (MDB_val const & val_a)
{
    assert (val_a.mv_size == sizeof (*this));
    static_assert (sizeof (head) + sizeof (staking_subchain_head) + sizeof (open_block) + sizeof (balance) + sizeof (modified) == sizeof (*this), "Class not packed");
    std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

logos::account_info_v5::account_info_v5 (logos::block_hash const & head_a, logos::block_hash const & staking_subchain_head_a, logos::block_hash const & open_block_a, logos::amount const & balance_a, uint64_t modified_a) :
head (head_a),
staking_subchain_head (staking_subchain_head_a),
open_block (open_block_a),
balance (balance_a),
modified (modified_a)
{
}

void logos::account_info_v5::serialize (logos::stream & stream_a) const
{
    write (stream_a, head.bytes);
    write (stream_a, staking_subchain_head.bytes);
    write (stream_a, open_block.bytes);
    write (stream_a, balance.bytes);
    write (stream_a, modified);
}

bool logos::account_info_v5::deserialize (logos::stream & stream_a)
{
    auto error (read (stream_a, head.bytes));
    if (!error)
    {
        error = read (stream_a, staking_subchain_head.bytes);
        if (!error)
        {
            error = read (stream_a, open_block.bytes);
            if (!error)
            {
                error = read (stream_a, balance.bytes);
                if (!error)
                {
                    error = read (stream_a, modified);
                }
            }
        }
    }
    return error;
}

logos::mdb_val logos::account_info_v5::val () const
{
    return logos::mdb_val (sizeof (*this), const_cast<logos::account_info_v5 *> (this));
}
