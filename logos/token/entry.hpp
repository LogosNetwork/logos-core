#pragma once

#include <logos/lib/numbers.hpp>
#include <logos/node/utility.hpp>

// TODO: Should eventually be
//       namespace qualified.
struct Entry
{
    Entry(bool & error,
          logos::stream & stream);

    uint32_t Serialize(logos::stream & stream) const;
    bool Deserialize(logos::stream & stream);

    logos::block_hash token_id;
    uint16_t          balance;
};
