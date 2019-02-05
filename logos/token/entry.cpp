#include <logos/token/entry.hpp>

BlockHash GetTokenUserId(const BlockHash & token_id, const AccountAddress & user)
{
    return Blake2bHash(TokenUserID(token_id, user));
}

TokenUserID::TokenUserID(const BlockHash & token_id,
                         const AccountAddress & user)
    : token_id(token_id)
    , user(user)
{}

void TokenUserID::Hash(blake2b_state & hash) const
{
    token_id.Hash(hash);
    user.Hash(hash);
}

TokenUserStatus::TokenUserStatus(bool & error,
                                 logos::stream & stream)
{
    error = Deserialize(stream);
}

TokenUserStatus::TokenUserStatus(bool & error, const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()), mdbval.size());
    error = Deserialize(stream);
}

uint32_t TokenUserStatus::Serialize(logos::stream & stream) const
{
    auto s = logos::write(stream, whitelisted);
    s += logos::write(stream, frozen);

    return s;
}

bool TokenUserStatus::Deserialize(logos::stream & stream)
{
    auto error = logos::read(stream, whitelisted);
    if(error)
    {
        return error;
    }

    return (error = logos::read(stream, frozen));
}

logos::mdb_val TokenUserStatus::ToMdbVal(std::vector<uint8_t> & buf) const
{
    assert(buf.empty());
    {
        logos::vectorstream stream(buf);
        Serialize(stream);
    }
    return logos::mdb_val(buf.size(), buf.data());
}

TokenEntry::TokenEntry(bool & error,
             logos::stream & stream)
{
    error = Deserialize(stream);
}

uint32_t TokenEntry::Serialize(logos::stream & stream) const
{
    auto s = logos::write(stream, token_id);
    s += status.Serialize(stream);
    s += logos::write(stream, balance);

    return s;
}

bool TokenEntry::Deserialize(logos::stream & stream)
{
    auto error = logos::read(stream, token_id);
    if(error)
    {
        return error;
    }

    error = status.Deserialize(stream);
    if(error)
    {
        return error;
    }

    return (error = logos::read(stream, balance));
}