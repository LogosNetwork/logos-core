#pragma once

#include <logos/node/utility.hpp>

struct RewardsInfo
{
    logos::mdb_val to_mdb_val(std::vector<uint8_t>& buf) const;

    uint32_t Serialize(logos::stream & stream) const;

    bool Deserialize(logos::stream & stream);

    bool     initialized;
    uint8_t  levy_percentage;
    Amount   total_stake;
    Amount   self_stake;
    Rational remaining_reward;
    Rational total_reward;
};

struct GlobalRewardsInfo
{
    logos::mdb_val to_mdb_val(std::vector<uint8_t>& buf) const;

    uint32_t Serialize(logos::stream & stream) const;

    bool Deserialize(logos::stream & stream);

    Amount   total_stake;
    Rational remaining_reward;
    Amount   total_reward;
};

