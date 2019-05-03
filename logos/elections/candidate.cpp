#include <logos/elections/candidate.hpp>

CandidateInfo::CandidateInfo() 
    : votes_received_weighted(0)
    , stake(0) 
    , epoch_modified(0)
{}

CandidateInfo::CandidateInfo(const AnnounceCandidacy& request)
    : votes_received_weighted(0)
    , bls_key(request.bls_key)
    , ecies_key(request.ecies_key)
    , stake(request.stake)
    , epoch_modified(request.epoch_num)
{
}

CandidateInfo::CandidateInfo(bool & error, const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(
            reinterpret_cast<uint8_t const *> (mdbval.data()), mdbval.size());
    error = deserialize (stream);
}

CandidateInfo::CandidateInfo(uint64_t votes) : 
    votes_received_weighted(votes)
{}

uint32_t CandidateInfo::serialize(logos::stream & stream) const
{
    auto val = logos::write(stream, votes_received_weighted);
    val += logos::write(stream, bls_key);
    val += ecies_key.Serialize(stream);
    val += logos::write(stream, stake);
    val += logos::write(stream, epoch_modified);
    return val;
}

bool CandidateInfo::deserialize(logos::stream & stream)
{
    bool error = logos::read(stream, votes_received_weighted);
    if(error)
    {
        return error;
    }
    error = logos::read(stream, bls_key);
    if(error)
    {
        return error;
    }
    error = ecies_key.Deserialize(stream);
    if(error)
    {
        return error;
    }
    error = logos::read(stream, stake);
    if(error)
    {
        return error;
    }
    return logos::read(stream, epoch_modified);
}
bool CandidateInfo::operator==(const CandidateInfo& other) const
{
    return votes_received_weighted == other.votes_received_weighted
        && bls_key == other.bls_key
        && ecies_key == other.ecies_key
        && stake == other.stake
        && epoch_modified == other.epoch_modified;
}
bool CandidateInfo::operator!=(const CandidateInfo& other) const
{
    return !(*this == other);
}
logos::mdb_val CandidateInfo::to_mdb_val(std::vector<uint8_t> & buf) const
{
    assert(buf.empty());
    {
        logos::vectorstream stream(buf);
        serialize(stream);
    }
    return logos::mdb_val(buf.size(), buf.data());
}

boost::property_tree::ptree CandidateInfo::SerializeJson() const
{
    boost::property_tree::ptree tree;
    tree.put("votes_received_weighted", votes_received_weighted.to_string());
    tree.put("bls_key", bls_key.to_string());
    ecies_key.SerializeJson(tree);
    tree.put("stake", stake.to_string());
    return tree;
}
