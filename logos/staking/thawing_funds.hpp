#pragma once
#include <logos/staking/liability.hpp>

//TODO move functions to cpp file, or at least this constant definition
const uint32_t max_uint32_t = 0 - 1;

struct ThawingFunds
{
    uint32_t expiration_epoch;
    AccountAddress target;
    Amount amount;
    LiabilityHash liability_hash;

    logos::mdb_val to_mdb_val(std::vector<uint8_t>& buf) const
    {
        assert(buf.empty());
        {
            logos::vectorstream stream(buf);
            Serialize(stream);
        }
        return logos::mdb_val(buf.size(), buf.data());
    }

    uint32_t Serialize(logos::stream & stream) const
    {
        uint32_t invert_exp_epoch = max_uint32_t - expiration_epoch;
        
        uint32_t s = logos::write(stream, invert_exp_epoch);;
        s += logos::write(stream, target);
        s += logos::write(stream, amount);
        s += logos::write(stream, liability_hash);
        return s;
    }

    bool Deserialize(logos::stream & stream)
    {
        uint32_t invert_exp_epoch = 0;
        bool error = logos::read(stream, invert_exp_epoch);
        expiration_epoch = max_uint32_t - invert_exp_epoch;
        return error
            || logos::read(stream, target)
            || logos::read(stream, amount)
            || logos::read(stream, liability_hash);
    }

    bool operator==(ThawingFunds const & other) const
    {
        return expiration_epoch == other.expiration_epoch
            && target == other.target
            && amount == other.amount
            && liability_hash == other.liability_hash;
    }

};
