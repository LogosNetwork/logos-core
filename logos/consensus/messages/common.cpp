#include <logos/consensus/messages/common.hpp>

AggSignature::AggSignature(bool & error, logos::stream & stream)
{
    unsigned long m;
    error = logos::read(stream, m);
    if(error)
    {
        return;
    }
    new (&map) ParicipationMap(le64toh(m));

    error = logos::read(stream, sig);
}

void AggSignature::Hash(blake2b_state & hash) const
{
    unsigned long m = htole64(map.to_ulong());
    blake2b_update(&hash, &m, sizeof(m));
    sig.Hash(hash);
}

uint32_t AggSignature::Serialize(logos::stream & stream) const
{
    unsigned long m = htole64(map.to_ulong());
    uint32_t s = logos::write(stream, m);
    s += logos::write(stream, sig);
    return s;
}

void AggSignature::SerializeJson(boost::property_tree::ptree & tree) const
{
    tree.put("paricipation_map", map.to_string());
    tree.put("signature", sig.to_string());
}

PrePrepareCommon::PrePrepareCommon()
    : primary_delegate()
    , epoch_number(0)
    , sequence(0)
    , timestamp(GetStamp())
    , previous()
    , preprepare_sig()
{}

PrePrepareCommon::PrePrepareCommon(bool & error, logos::stream & stream)
{
    error = logos::read(stream, primary_delegate);
    if(error)
    {
        return;
    }

    error = logos::read(stream, epoch_number);
    if(error)
    {
        return;
    }
    epoch_number = le32toh(epoch_number);

    error = logos::read(stream, sequence);
    if(error)
    {
        return;
    }
    sequence = le32toh(sequence);

    error = logos::read(stream, timestamp);
    if(error)
    {
        return;
    }
    timestamp = le64toh(timestamp);

    error = logos::read(stream, previous);
    if(error)
    {
        return;
    }

    error = logos::read(stream, preprepare_sig);
}

PrePrepareCommon & PrePrepareCommon::operator= (const PrePrepareCommon & other)
{
    primary_delegate = other.primary_delegate;
    epoch_number     = other.epoch_number;
    sequence         = other.sequence;
    timestamp        = other.timestamp;
    previous         = other.previous;
    preprepare_sig   = other.preprepare_sig;

    return *this;
}

void PrePrepareCommon::Hash(blake2b_state & hash) const
{
    uint32_t en = htole32(epoch_number);
    uint32_t sqn = htole32(sequence);
    uint64_t tsp = htole64(timestamp);

    primary_delegate.Hash(hash);
    blake2b_update(&hash, &en, sizeof(uint32_t));
    blake2b_update(&hash, &sqn, sizeof(uint32_t));
    blake2b_update(&hash, &tsp, sizeof(uint64_t));
    previous.Hash(hash);
}

uint32_t PrePrepareCommon::Serialize(logos::stream & stream) const
{
    uint32_t en = htole32(epoch_number);
    uint32_t sqn = htole32(sequence);
    uint64_t tsp = htole64(timestamp);

    auto s = logos::write(stream, primary_delegate);
    s += logos::write(stream, en);
    s += logos::write(stream, sqn);
    s += logos::write(stream, tsp);
    s += logos::write(stream, previous);
    s += logos::write(stream, preprepare_sig);

    return s;
}

void PrePrepareCommon::SerializeJson(boost::property_tree::ptree & tree) const
{
    tree.put("delegate", primary_delegate.to_string());
    tree.put("epoch_number", std::to_string(epoch_number));
    tree.put("sequence", std::to_string(sequence));
    tree.put("timestamp", std::to_string(timestamp));
    tree.put("previous", previous.to_string());
    tree.put("signature", preprepare_sig.to_string());
}