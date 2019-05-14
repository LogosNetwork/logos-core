#include <logos/staking/staked_funds.hpp>
#include <logos/lib/trace.hpp>

StakedFunds::StakedFunds() : target(0), amount(0), liability_hash(0) {}

StakedFunds::StakedFunds(boost::optional<StakedFunds> const & option)
{
    if(option)
    {
        *this = option.get();
    }
    else
    {
        Log log;
        LOG_FATAL(log) << "StakedFunds::StakedFunds - option is empty";
        trace_and_halt();
    }
}

StakedFunds StakedFunds::operator=(boost::optional<StakedFunds> const & option)
{
    if(option)
    {
        *this = option.get();
    }
    else
    {
        Log log;
        LOG_FATAL(log) << "StakedFunds::StakedFunds - option is empty";
        trace_and_halt();
    }
}

logos::mdb_val StakedFunds::to_mdb_val(std::vector<uint8_t>& buf) const
{
    assert(buf.empty());
    {
        logos::vectorstream stream(buf);
        Serialize(stream);
    }
    return logos::mdb_val(buf.size(), buf.data());
}

uint32_t StakedFunds::Serialize(logos::stream & stream) const
{

    uint32_t s = logos::write(stream, target);;
    s += logos::write(stream, amount);
    s += logos::write(stream, liability_hash);
    return s;
}

bool StakedFunds::Deserialize(logos::stream & stream)
{
    return logos::read(stream, target)
        || logos::read(stream, amount)
        || logos::read(stream, liability_hash);
}

