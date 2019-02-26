#include <logos/elections/database.hpp>

//*** begin RepInfo functions ***

RepInfo::RepInfo() 
    : candidacy_action_tip(0)
    , election_vote_tip(0)
    , rep_action_tip (0)
{}

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
    s += logos::write(stream, last_epoch_voted);
    s += logos::write(stream, announced_stop);
    s += logos::write(stream, rep_action_epoch);
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

    error = logos::read(stream, last_epoch_voted);
    if(error)
    {
        return error;
    }
    error = logos::read(stream, announced_stop);
    if(error)
    {
        return error;
    }
    error = logos::read(stream, rep_action_epoch);
    if(error)
    {
        return true;
    }
    return logos::read(stream, stake);
}

bool RepInfo::operator== (RepInfo const & other) const
{
    return candidacy_action_tip == other.candidacy_action_tip
        && election_vote_tip == other.election_vote_tip
        && rep_action_tip == other.rep_action_tip;
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
//*** end RepInfo functions ***


//*** begin CandidateInfo functions ***

CandidateInfo::CandidateInfo(bool & error, const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(
            reinterpret_cast<uint8_t const *> (mdbval.data()), mdbval.size());
    error = deserialize (stream);
}

CandidateInfo::CandidateInfo(bool active,bool remove,uint64_t votes) : 
    active(active),
    remove(remove),
    votes_received_weighted(votes)
{}

uint32_t CandidateInfo::serialize(logos::stream & stream) const
{
    auto val = logos::write(stream, active);
    val += logos::write(stream, remove);
    val += logos::write(stream, votes_received_weighted);
    return val;
}

bool CandidateInfo::deserialize(logos::stream & stream)
{
    bool error = logos::read(stream, active);
    if(error)
    {
        return error;
    }
    error = logos::read(stream, remove);
    if(error)
    {
        return error;
    }
    return logos::read(stream, votes_received_weighted);
}
bool CandidateInfo::operator==(const CandidateInfo& other) const
{
    return 
        active == other.active && 
        remove == other.remove && 
        votes_received_weighted == other.votes_received_weighted;
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
//*** end CandidateInfo functions


