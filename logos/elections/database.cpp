#include <logos/elections/database.hpp>

RepInfo::RepInfo() 
    : candidacy_action_tip(0)
    , election_vote_tip(0)
    , rep_action_tip (0)
    , stake(0)
{}

RepInfo::RepInfo(const StartRepresenting& request)
    : RepInfo()
{
    rep_action_tip = request.Hash();
    stake = request.stake;
}

RepInfo::RepInfo(bool & error, const logos::mdb_val & mdbval)
{

    logos::bufferstream stream(
            reinterpret_cast<uint8_t const *> (mdbval.data()), mdbval.size());
    error = deserialize (stream);
}

RepInfo::RepInfo (
        logos::block_hash const & candidacy_action_tip,
        logos::block_hash const & election_vote_tip,
        logos::block_hash const & rep_action_tip)
    : candidacy_action_tip(candidacy_action_tip)
    , election_vote_tip(election_vote_tip)
    , rep_action_tip(rep_action_tip)
{}

uint32_t RepInfo::serialize (logos::stream & stream) const
{
    //TODO remove auto
    auto s = logos::write(stream, candidacy_action_tip.bytes);
    s += logos::write(stream, election_vote_tip.bytes);
    s += logos::write(stream, rep_action_tip.bytes);
    s += logos::write(stream, stake);
    return s;
}

bool RepInfo::deserialize (logos::stream & stream)
{
    //TODO remove auto
    auto error (logos::read (stream, candidacy_action_tip.bytes));
    if(error)
    {
        return error;
    }

    error = logos::read (stream, election_vote_tip.bytes);
    if(error)
    {
        return error;
    }

    error = logos::read(stream, rep_action_tip.bytes);
    if(error)
    {
        return error;
    }
    return logos::read(stream, stake);
}

bool RepInfo::operator== (RepInfo const & other) const
{
    return candidacy_action_tip == other.candidacy_action_tip
        && election_vote_tip == other.election_vote_tip
        && rep_action_tip == other.rep_action_tip
        && stake == other.stake;
}

bool RepInfo::operator!= (RepInfo const & other) const
{
    return !(*this == other);
}

logos::mdb_val RepInfo::to_mdb_val(std::vector<uint8_t> &buf) const
{
    assert(buf.empty());
    {
        logos::vectorstream stream(buf);
        serialize(stream);
    }
    return logos::mdb_val(buf.size(), buf.data());
}

boost::property_tree::ptree RepInfo::SerializeJson() const
{
    boost::property_tree::ptree tree;
    tree.put("candidacy_action_tip",candidacy_action_tip.to_string());
    tree.put("election_vote_tip",election_vote_tip.to_string());
    tree.put("rep_action_tip",rep_action_tip.to_string());
    tree.put("stake",stake.to_string()); 
    return tree;
}


CandidateInfo::CandidateInfo() 
    : votes_received_weighted(0)
    , stake(0) 
    , epoch_modified(0)
{}

CandidateInfo::CandidateInfo(const AnnounceCandidacy& request)
    : votes_received_weighted(0)
    , bls_key(request.bls_key)
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
    tree.put("stake", stake.to_string());
    return tree;
}
