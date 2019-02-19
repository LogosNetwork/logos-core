#pragma once

#include <logos/node/utility.hpp>
#include <logos/lib/numbers.hpp>
#include <logos/lib/hash.hpp>

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

    BlockHash       token_id;
    TokenUserStatus status;
    Amount          balance = 0;
};

class TokenIssuance;

BlockHash GetTokenID(const std::string & symbol,
                     const std::string & name,
                     const AccountAddress & issuer,
                     const BlockHash & previous);

BlockHash GetTokenID(const TokenIssuance & issuance);

struct TokenID
{
    TokenID(const std::string & symbol,
            const std::string & name,
            const AccountAddress & issuer,
            const BlockHash & previous);

    TokenID(const TokenIssuance & issuance);

    void Hash(blake2b_state & hash) const;

    std::string    symbol;
    std::string    name;
    AccountAddress issuer;
    BlockHash      previous;
};

BlockHash GetTokenUserID(const BlockHash & token_id,
                         const AccountAddress & user);

struct TokenUserID
{
    TokenUserID(const BlockHash & token_id,
                const AccountAddress & user);

    void Hash(blake2b_state & hash) const;

    BlockHash      token_id;
    AccountAddress user;
};
