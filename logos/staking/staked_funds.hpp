#pragma once
#include <logos/staking/liability.hpp>
struct StakedFunds
{
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

        uint32_t s = logos::write(stream, target);;
        s += logos::write(stream, amount);
        s += logos::write(stream, liability_hash);
        return s;
    }

    bool Deserialize(logos::stream & stream)
    {
        return logos::read(stream, target)
            || logos::read(stream, amount)
            || logos::read(stream, liability_hash);
    }

};
