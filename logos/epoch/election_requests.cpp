#include <logos/epoch/election_requests.hpp>
#include <logos/request/fields.hpp>

AnnounceCandidacy::AnnounceCandidacy(
            const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountPrivKey & priv,
            const AccountPubKey & pub)
    : Request(
            RequestType::AnnounceCandidacy,
            origin, previous, fee, sequence, priv, pub)
{}  

AnnounceCandidacy::AnnounceCandidacy(
            const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountSig & sig)
    : Request(
            RequestType::AnnounceCandidacy,
            origin, previous, fee, sequence, sig)
{}  

AnnounceCandidacy::AnnounceCandidacy(bool & error,
            std::basic_streambuf<uint8_t> & stream) : Request(error, stream)
{
    //ensure type is correct
    error = error || type != RequestType::AnnounceCandidacy;
}

AnnounceCandidacy::AnnounceCandidacy(bool & error,
            boost::property_tree::ptree const & tree) : Request(error, tree)
{
    error = error || type != RequestType::AnnounceCandidacy;
}

RenounceCandidacy::RenounceCandidacy(
            const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountPrivKey & priv,
            const AccountPubKey & pub)
    : Request(
            RequestType::RenounceCandidacy,
            origin, previous, fee, sequence, priv, pub)
{}  

RenounceCandidacy::RenounceCandidacy(
            const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountSig & sig)
    : Request(
            RequestType::RenounceCandidacy,
            origin, previous, fee, sequence, sig)
{} 

RenounceCandidacy::RenounceCandidacy(bool & error,
            std::basic_streambuf<uint8_t> & stream) : Request(error, stream)
{
    error = error || type != RequestType::RenounceCandidacy;
}

RenounceCandidacy::RenounceCandidacy(bool & error,
            boost::property_tree::ptree const & tree) : Request(error, tree)
{
    error = error || type != RequestType::RenounceCandidacy;
}


ElectionVote::ElectionVote(
        const AccountAddress & origin,
        const BlockHash & previous,
        const Amount & fee,
        uint32_t sequence,
        const AccountPrivKey & priv,
        const AccountPubKey & pub)
    : Request(
            RequestType::ElectionVote,
            origin, previous, fee, sequence, priv, pub)
{}

ElectionVote::ElectionVote(
        const AccountAddress & origin,
        const BlockHash & previous,
        const Amount & fee,
        uint32_t sequence,
        const AccountSig & signature)
    : Request(
            RequestType::ElectionVote,
            origin, previous, fee, sequence, signature)
{}

ElectionVote::ElectionVote(bool & error,
            std::basic_streambuf<uint8_t> & stream) : Request(error, stream)
{
    if(error)
    {
        return;
    } 

    uint8_t count;
    error = logos::read(stream, count);
    if(error)
    {
        return;
    }

    for(size_t i = 0; i < count; ++i)
    {
        CandidateVotePair vote(error, stream);
        if(error)
        {
            return;
        }
        votes_.push_back(vote);
    }
}


ElectionVote::ElectionVote(bool & error,
            boost::property_tree::ptree const & tree)
    : Request(error, tree)
{
    if(error)
    {
        return;
    } 
    try {
        auto votes = tree.get_child("request.votes");
        for(const std::pair<std::string,boost::property_tree::ptree> &v : votes)
        {
            auto account_s (v.first);
            AccountAddress candidate;
            error = candidate.decode_account(account_s);
            if(error)
            {
                break;
            }
            uint8_t vote_val;
            vote_val = std::stoi(v.second.data());
            CandidateVotePair vp(candidate, vote_val);
            votes_.push_back(vp);
        }
    }
    catch(std::runtime_error const &)
    {
        error = true;
    }
}

ElectionVote::ElectionVote(bool & error,
                           const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *> (mdbval.data()),
            mdbval.size());

    new (this) ElectionVote(error, stream);
}


void ElectionVote::Hash(blake2b_state& hash) const
{
    Request::Hash(hash);
    //TODO: .size() can return bigger than uint8_t
    uint8_t count = votes_.size();
    blake2b_update(&hash, &count, sizeof(count));
   
    for(const auto & v : votes_)
    {
        v.account.Hash(hash);
        blake2b_update(&hash, &(v.num_votes),sizeof(v.num_votes));
    }
}

uint16_t ElectionVote::WireSize() const
{
    return sizeof(uint8_t) + (votes_.size() * CandidateVotePair::WireSize())
        + Request::WireSize();
}


boost::property_tree::ptree ElectionVote::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree votes_tree;
    for(const auto & v : votes_)
    {
        votes_tree.put(v.account.to_account()
                ,std::to_string(v.num_votes));
    }
    boost::property_tree::ptree tree;
    tree.add_child(VOTES,votes_tree);
    
    boost::property_tree::ptree super_tree(Request::SerializeJson());
    super_tree.add_child(REQUEST,tree);
    return super_tree; 
}


uint64_t ElectionVote::Serialize(logos::stream & stream) const
{
   
   uint64_t val = Request::Serialize(stream);
   uint8_t count = votes_.size(); //TODO: this is not safe if size is too big
   val += logos::write(stream, count);
   for(auto const & v : votes_)
   {
        val += v.Serialize(stream);
   }
   return val;
}

bool ElectionVote::operator==(const ElectionVote& other) const
{
    return votes_ == other.votes_ && Request::operator==(other);
}

bool ElectionVote::operator!=(const ElectionVote& other) const
{
    return !(*this == other);
}
