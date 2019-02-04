#pragma once

#include <logos/node/utility.hpp>
#include <logos/lib/numbers.hpp>

struct TokenUserStatus
{
    TokenUserStatus() = default;

    TokenUserStatus(bool & error,
                    logos::stream & stream);

    TokenUserStatus(bool & error,
                    const logos::mdb_val & mdbval);

    uint32_t Serialize(logos::stream & stream) const;
    bool Deserialize(logos::stream & stream);

    bool whitelisted;
    bool frozen;
};

// TODO: Should eventually be
//       namespace qualified.
struct Entry
{
    Entry(bool & error,
          logos::stream & stream);

    uint32_t Serialize(logos::stream & stream) const;
    bool Deserialize(logos::stream & stream);

    logos::block_hash token_id;
    TokenUserStatus   status;
    uint16_t          balance;
};
