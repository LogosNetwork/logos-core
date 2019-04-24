#pragma once

#include <logos/common.hpp>

using LiabilityHash = logos::uint256_union;

struct Liability
{
    AccountAddress target;
    AccountAddress source;
    Amount amount;
    uint32_t expiration_epoch;

    LiabilityHash Hash() const;

    void Hash(blake2b_state& hash) const;
};
