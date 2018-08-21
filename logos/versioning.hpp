#pragma once

#include <logos/lib/blocks.hpp>
#include <logos/node/utility.hpp>

namespace logos
{
class account_info_v1
{
public:
    account_info_v1 ();
    account_info_v1 (MDB_val const &);
    account_info_v1 (logos::account_info_v1 const &) = default;
    account_info_v1 (logos::block_hash const &, logos::block_hash const &, logos::amount const &, uint64_t);
    void serialize (logos::stream &) const;
    bool deserialize (logos::stream &);
    logos::mdb_val val () const;
    logos::block_hash head;
    logos::block_hash rep_block;
    logos::amount balance;
    uint64_t modified;
};
class pending_info_v3
{
public:
    pending_info_v3 ();
    pending_info_v3 (MDB_val const &);
    pending_info_v3 (logos::account const &, logos::amount const &, logos::account const &);
    void serialize (logos::stream &) const;
    bool deserialize (logos::stream &);
    bool operator== (logos::pending_info_v3 const &) const;
    logos::mdb_val val () const;
    logos::account source;
    logos::amount amount;
    logos::account destination;
};
// Latest information about an account
class account_info_v5
{
public:
    account_info_v5 ();
    account_info_v5 (MDB_val const &);
    account_info_v5 (logos::account_info_v5 const &) = default;
    account_info_v5 (logos::block_hash const &, logos::block_hash const &, logos::block_hash const &, logos::amount const &, uint64_t);
    void serialize (logos::stream &) const;
    bool deserialize (logos::stream &);
    logos::mdb_val val () const;
    logos::block_hash head;
    logos::block_hash rep_block;
    logos::block_hash open_block;
    logos::amount balance;
    uint64_t modified;
};
}
