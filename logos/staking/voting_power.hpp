#pragma once


struct VotingPowerFallback
{
    Amount power;
    Amount total_stake;

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

        return logos::write(stream, power) + logos::write(stream, total_stake);
    }

    bool Deserialize(logos::stream & stream)
    {
        return logos::read(stream, power) || logos::read(stream, total_stake);
    }

};

struct VotingPowerSnapshot
{

    Amount locked_proxied;
    Amount unlocked_proxied;
    Amount self_stake;

    VotingPowerSnapshot(
            Amount const & locked,
            Amount const & unlocked,
            Amount const & self)
        : locked_proxied(locked)
        , unlocked_proxied(unlocked)
        , self_stake(self)
    {}

    VotingPowerSnapshot()
        : locked_proxied(0)
        , unlocked_proxied(0)
        , self_stake(0)
    {}

    uint32_t Serialize(logos::stream & stream) const
    {
        uint32_t s = logos::write(stream, locked_proxied);
        s += logos::write(stream, unlocked_proxied);
        s += logos::write(stream, self_stake);
        return s;
    }

    bool Deserialize(logos::stream & stream)
    {
        return logos::read(stream, locked_proxied)
            || logos::read(stream, unlocked_proxied)
            || logos::read(stream, self_stake);
    }


};

struct VotingPowerInfo
{
    VotingPowerSnapshot current;
    VotingPowerSnapshot next;
    uint32_t epoch_modified;

    VotingPowerInfo(
            VotingPowerSnapshot const & cur,
            VotingPowerSnapshot const & next,
            uint32_t const & epoch)
        : current(cur)
        , next(next)
        , epoch_modified(epoch)
    {}

    VotingPowerInfo() : current(), next(), epoch_modified(0) {}

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

        uint32_t s = current.Serialize(stream);
        s += next.Serialize(stream);
        s += logos::write(stream, epoch_modified);
        return s;
    }

    bool Deserialize(logos::stream & stream)
    {
        return current.Deserialize(stream)
            || next.Deserialize(stream)
            || logos::read(stream, epoch_modified);
    }


};

