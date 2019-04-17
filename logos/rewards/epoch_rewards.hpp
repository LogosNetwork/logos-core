#pragma once

struct EpochRewardsInfo
{
    uint8_t levy_percentage;
    Amount total_stake;
    Amount remaining_reward;
    Amount total_reward;


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
        uint32_t s = logos::write(stream, levy_percentage);
        s += logos::write(stream, total_stake);
        s += logos::write(stream, remaining_reward);
        s += logos::write(stream, total_reward); 
        return s;
    }

    bool Deserialize(logos::stream & stream)
    {
        return logos::read(stream, levy_percentage)
            || logos::read(stream, total_stake)
            || logos::read(stream, remaining_reward)
            || logos::read(stream, total_reward); 

    }
};

