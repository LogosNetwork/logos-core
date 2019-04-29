#include <logos/consensus/messages/tip.hpp>

Tip::Tip()
: epoch(0)
, sqn(0)
, digest(0)
{}

Tip::Tip(uint32_t epoch, uint32_t sqn, const BlockHash & digest)
: epoch(epoch)
, sqn(sqn)
, digest(digest)
{}

Tip::Tip(bool & error, logos::stream & stream)
{
    error = logos::read(stream, epoch);
    if(error)
    {
        return;
    }
    error = logos::read(stream, sqn);
    if(error)
    {
        return;
    }
    error = logos::read(stream, digest);
}

Tip::Tip(bool & error, logos::mdb_val & mdbval)
{
    logos::bufferstream stream(
            reinterpret_cast<uint8_t const *> (mdbval.data()),
            mdbval.size());
    new (this) Tip(error, stream);
}

uint32_t Tip::Serialize(logos::stream & stream) const
{
    auto s = logos::write(stream, epoch);
    s += logos::write(stream, sqn);
    s += logos::write(stream, digest);

    assert(s == WireSize);
    return s;
}
logos::mdb_val Tip::to_mdb_val(std::vector<uint8_t> &buf) const
{
    {
        logos::vectorstream stream(buf);
        Serialize(stream);
    }
    return logos::mdb_val(buf.size(), buf.data());
}

void Tip::Hash(blake2b_state & hash) const
{
    blake2b_update(&hash, &epoch, sizeof(epoch));
    blake2b_update(&hash, &sqn, sizeof(sqn));
    digest.Hash(hash);
}

bool Tip::operator<(const Tip & other) const
{
    return epoch < other.epoch ||
            (epoch == other.epoch && sqn < other.sqn) ||
            (epoch == other.epoch && sqn==0 && other.sqn==0 && digest==0 && other.digest!=0);
}

bool Tip::operator==(const Tip & other) const
{
    return epoch==other.epoch && sqn==other.sqn && digest==other.digest;
}

bool Tip::operator!=(const Tip & other) const
{
    return !(*this==other);
}

void Tip::clear()
{
    epoch = 0;
    sqn = 0;
    digest = 0;
}

std::string Tip::to_string () const
{
    std::stringstream stream;
    stream << epoch << ":" << sqn << ":"<< digest.to_string();

    return stream.str ();
}
