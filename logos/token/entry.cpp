#include <logos/token/entry.hpp>

Entry::Entry(bool & error,
             logos::stream & stream)
{
    error = Deserialize(stream);
}

uint32_t Entry::Serialize(logos::stream & stream) const
{
    auto s = logos::write(stream, token_id);
    s += logos::write(stream, balance);
    return s;
}

bool Entry::Deserialize(logos::stream & stream)
{
    auto error = logos::read(stream, token_id);
    if(error)
    {
        return true;
    }

    return (error = logos::read(stream, balance));
}
