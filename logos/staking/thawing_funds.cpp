#include <logos/staking/thawing_funds.hpp>

//ThawingFunds are stored in reverse order of expiration (expiring latest is
//stored first). LMDB stores records in lexicographic order, so if expiration_epoch
//is the first field, LMDB actually stores ThawingFunds in order of expiration,
//which is the opposite of what we want. To change the order, we "invert" the
//expiration epoch, by subtracting expiration epoch from max_uint32_t

logos::mdb_val ThawingFunds::to_mdb_val(std::vector<uint8_t>& buf) const
{
    assert(buf.empty());
    {
        logos::vectorstream stream(buf);
        Serialize(stream);
    }
    return logos::mdb_val(buf.size(), buf.data());
}

uint32_t ThawingFunds::Serialize(logos::stream & stream) const
{
    uint32_t invert_exp_epoch = std::numeric_limits<uint32_t>::max() - expiration_epoch;
    
    uint32_t s = logos::write(stream, invert_exp_epoch);;
    s += logos::write(stream, target);
    s += logos::write(stream, amount);
    s += logos::write(stream, liability_hash);
    return s;
}

bool ThawingFunds::Deserialize(logos::stream & stream)
{
    uint32_t invert_exp_epoch = 0;
    bool error = logos::read(stream, invert_exp_epoch);
    expiration_epoch = std::numeric_limits<uint32_t>::max() - invert_exp_epoch;
    return error
        || logos::read(stream, target)
        || logos::read(stream, amount)
        || logos::read(stream, liability_hash);
}

bool ThawingFunds::operator==(ThawingFunds const & other) const
{
    return expiration_epoch == other.expiration_epoch
        && target == other.target
        && amount == other.amount
        && liability_hash == other.liability_hash;
}
