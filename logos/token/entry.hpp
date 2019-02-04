#pragma once

#include <logos/node/utility.hpp>
#include <logos/lib/numbers.hpp>
#include <logos/lib/hash.hpp>

BlockHash GetTokenUserId(const BlockHash & token_id,
                         const AccountAddress & user);

struct TokenUserID
{
    TokenUserID(const BlockHash & token_id,
                const AccountAddress & user);

    void Hash(blake2b_state & hash) const;

    BlockHash      token_id;
    AccountAddress user;
};

struct TokenUserStatus
{
    TokenUserStatus() = default;

    TokenUserStatus(bool & error,
                    logos::stream & stream);

    TokenUserStatus(bool & error,
                    const logos::mdb_val & mdbval);

    uint32_t Serialize(logos::stream & stream) const;
    bool Deserialize(logos::stream & stream);

    logos::mdb_val ToMdbVal(std::vector<uint8_t> & buf) const;

    bool whitelisted = false;
    bool frozen      = false;
};

// TODO: Should eventually be
//       namespace qualified.
struct TokenEntry
{
    TokenEntry() = default;

    TokenEntry(bool & error,
          logos::stream & stream);

    uint32_t Serialize(logos::stream & stream) const;
    bool Deserialize(logos::stream & stream);

    logos::block_hash token_id;
    TokenUserStatus   status;
    uint16_t          balance;
};
