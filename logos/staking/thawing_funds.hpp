#pragma once
#include <logos/staking/liability.hpp>

struct ThawingFunds
{
    uint32_t expiration_epoch;
    AccountAddress target;
    Amount amount;
    LiabilityHash liability_hash;

    logos::mdb_val to_mdb_val(std::vector<uint8_t>& buf) const;

    uint32_t Serialize(logos::stream & stream) const;

    bool Deserialize(logos::stream & stream);

    bool operator==(ThawingFunds const & other) const;

};
