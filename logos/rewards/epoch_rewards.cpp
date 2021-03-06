#include <logos/rewards/epoch_rewards.hpp>

logos::mdb_val RewardsInfo::to_mdb_val(std::vector<uint8_t>& buf) const
{
    assert(buf.empty());
    {
        logos::vectorstream stream(buf);
        Serialize(stream);
    }
    return logos::mdb_val(buf.size(), buf.data());
}

uint32_t RewardsInfo::Serialize(logos::stream & stream) const
{
    uint32_t s = logos::write(stream, initialized);
    s += logos::write(stream, levy_percentage);
    s += logos::write(stream, total_stake);
    s += logos::write(stream, self_stake);
    s += logos::write(stream, remaining_reward);
    s += logos::write(stream, total_reward);
    return s;
}

bool RewardsInfo::Deserialize(logos::stream & stream)
{
    return logos::read(stream, initialized)
           || logos::read(stream, levy_percentage)
           || logos::read(stream, total_stake)
           || logos::read(stream, self_stake)
           || logos::read(stream, remaining_reward)
           || logos::read(stream, total_reward);
}

logos::mdb_val GlobalRewardsInfo::to_mdb_val(std::vector<uint8_t>& buf) const
{
    assert(buf.empty());

    {
        logos::vectorstream stream(buf);
        Serialize(stream);
    }

    return logos::mdb_val(buf.size(), buf.data());
}

uint32_t GlobalRewardsInfo::Serialize(logos::stream & stream) const
{
    uint32_t s = logos::write(stream, total_stake);
    s += logos::write(stream, remaining_reward);
    s += logos::write(stream, total_reward);

    return s;
}

bool GlobalRewardsInfo::Deserialize(logos::stream & stream)
{
    return logos::read(stream, total_stake)
                  || logos::read(stream, remaining_reward)
                  || logos::read(stream, total_reward);
}
