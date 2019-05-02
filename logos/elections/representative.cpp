#include <logos/elections/representative.hpp>

RepInfo::RepInfo() 
    : candidacy_action_tip(0)
    , election_vote_tip(0)
    , rep_action_tip (0)
    , levy_percentage(100)
{}

RepInfo::RepInfo(const StartRepresenting& request)
    : RepInfo()
{
    rep_action_tip = request.GetHash();
}

RepInfo::RepInfo(const AnnounceCandidacy& request)
    : RepInfo()
{
    rep_action_tip = request.GetHash();
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
    auto s = logos::write(stream, candidacy_action_tip.bytes);
    s += logos::write(stream, rep_action_tip.bytes);
    s += logos::write(stream, election_vote_tip.bytes);
    s += logos::write(stream, levy_percentage);
    return s;
}

bool RepInfo::deserialize (logos::stream & stream)
{
    return logos::read(stream, candidacy_action_tip.bytes)
        || logos::read(stream, rep_action_tip.bytes)
        || logos::read(stream, election_vote_tip.bytes)
        || logos::read(stream, levy_percentage);
}

bool RepInfo::operator== (RepInfo const & other) const
{
    return candidacy_action_tip == other.candidacy_action_tip
        && election_vote_tip == other.election_vote_tip
        && rep_action_tip == other.rep_action_tip
        && levy_percentage == other.levy_percentage;
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
    tree.put("levy_percentage", levy_percentage);
    return tree;
}


