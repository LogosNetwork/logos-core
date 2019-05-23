#pragma once
#include <logos/staking/liability.hpp>
struct StakedFunds
{
    AccountAddress target;
    Amount amount;
    LiabilityHash liability_hash;

    StakedFunds();

    logos::mdb_val to_mdb_val(std::vector<uint8_t>& buf) const;

    uint32_t Serialize(logos::stream & stream) const;

    bool Deserialize(logos::stream & stream);

};
