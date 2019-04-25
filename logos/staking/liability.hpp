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
        s += logos::write(stream, source);
        s += logos::write(stream, amount);
        s += logos::write(stream, expiration_epoch);
        return s;
    }

    bool Deserialize(logos::stream & stream)
    {
        return logos::read(stream, target)
            || logos::read(stream, source)
            || logos::read(stream, amount)
            || logos::read(stream, expiration_epoch);
    }


};
