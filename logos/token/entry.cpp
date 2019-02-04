#include <logos/token/entry.hpp>

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

Entry::Entry(bool & error,
             logos::stream & stream)
{
    error = Deserialize(stream);
}

uint32_t Entry::Serialize(logos::stream & stream) const
{
    auto s = logos::write(stream, token_id);
    s += status.Serialize(stream);
    s += logos::write(stream, balance);

    return s;
}

bool Entry::Deserialize(logos::stream & stream)
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
