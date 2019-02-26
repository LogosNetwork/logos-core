#include <logos/elections/requests.hpp>
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
{
    Hash();
}  

AnnounceCandidacy::AnnounceCandidacy(
            const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountSig & sig)
    : Request(
            RequestType::AnnounceCandidacy,
            origin, previous, fee, sequence, sig)
{
    Hash();
}  

AnnounceCandidacy::AnnounceCandidacy(bool & error,
            std::basic_streambuf<uint8_t> & stream) : Request(error, stream)
{
    //ensure type is correct
    error = error || type != RequestType::AnnounceCandidacy;
    Hash();
}

AnnounceCandidacy::AnnounceCandidacy(bool & error,
            boost::property_tree::ptree const & tree) : Request(error, tree)
{
    error = error || type != RequestType::AnnounceCandidacy;
    Hash();
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
{
    Hash();
}  

RenounceCandidacy::RenounceCandidacy(
            const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountSig & sig)
    : Request(
            RequestType::RenounceCandidacy,
            origin, previous, fee, sequence, sig)
{

    Hash();
} 

RenounceCandidacy::RenounceCandidacy(bool & error,
            std::basic_streambuf<uint8_t> & stream) : Request(error, stream)
{
    error = error || type != RequestType::RenounceCandidacy;
    Hash();
}

RenounceCandidacy::RenounceCandidacy(bool & error,
            boost::property_tree::ptree const & tree) : Request(error, tree)
{
    error = error || type != RequestType::RenounceCandidacy;
    Hash();
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
{
    Hash();
}

ElectionVote::ElectionVote(
        const AccountAddress & origin,
        const BlockHash & previous,
        const Amount & fee,
        uint32_t sequence,
        const AccountSig & signature)
    : Request(
            RequestType::ElectionVote,
            origin, previous, fee, sequence, signature)
{
    Hash();
}

ElectionVote::ElectionVote(bool & error,
            std::basic_streambuf<uint8_t> & stream) : Request(error, stream)
{
    if(error)
    {
        return;
    } 
    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    Hash();


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
        Hash();
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

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Hash();
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
   uint8_t count = votes_.size(); //TODO: this is not safe if size is too big
   uint64_t val = logos::write(stream, count);
   for(auto const & v : votes_)
   {
        val += v.Serialize(stream);
   }
   return val;
}

void ElectionVote::Deserialize(bool & error, logos::stream & stream)
{
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

void ElectionVote::DeserializeDB(bool & error, logos::stream & stream)
{
    Request::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

bool ElectionVote::operator==(const ElectionVote& other) const
{
    return votes_ == other.votes_ && Request::operator==(other);
}

bool ElectionVote::operator!=(const ElectionVote& other) const
{
    return !(*this == other);
}

StartRepresenting::StartRepresenting(
        const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountPrivKey & priv,
            const AccountPubKey & pub,
            const Amount stake)
    : Request(RequestType::StartRepresenting, origin, previous, fee, sequence, priv, pub), stake(stake)
{}

StartRepresenting::StartRepresenting(
        const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountSig & signature,
            const Amount stake)
    : Request(RequestType::StartRepresenting, origin, previous, fee, sequence, signature), stake(stake)
{}


StartRepresenting::StartRepresenting(bool & error,
            std::basic_streambuf<uint8_t> & stream)
    : Request(error, stream)
{

    if(error)
    {
        return;
    } 
    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

StartRepresenting::StartRepresenting(bool& error, const logos::mdb_val& mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *> (mdbval.data()),
            mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}


StartRepresenting::StartRepresenting(bool & error,
            boost::property_tree::ptree const & tree)
    : Request(error, tree)
{
   if(error)
   {
        return;
   } 
   try
   {
        stake = tree.get<std::string>("stake");
   }
   catch(...)
   {
    error = true;
   }
}

void StartRepresenting::Deserialize(bool& error, logos::stream& stream)
{
    error = logos::read(stream,stake);
    
}

void StartRepresenting::DeserializeDB(bool& error, logos::stream& stream)
{
    Request::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}


StopRepresenting::StopRepresenting(
        const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountPrivKey & priv,
            const AccountPubKey & pub)
    : Request(RequestType::StopRepresenting, origin, previous, fee, sequence, priv, pub)
{}

StopRepresenting::StopRepresenting(
        const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountSig & signature)
    : Request(RequestType::StopRepresenting, origin, previous, fee, sequence, signature)
{}


StopRepresenting::StopRepresenting(bool & error,
            std::basic_streambuf<uint8_t> & stream)
    : Request(error, stream)
{

    if(error)
    {
        return;
    } 
    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

StopRepresenting::StopRepresenting(bool& error, const logos::mdb_val& mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *> (mdbval.data()),
            mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}


StopRepresenting::StopRepresenting(bool & error,
            boost::property_tree::ptree const & tree)
    : Request(error, tree)
{
   if(error)
   {
        return;
   } 
}

void StopRepresenting::Deserialize(bool& error, logos::stream& stream)
{
    
}

void StopRepresenting::DeserializeDB(bool& error, logos::stream& stream)
{
    Request::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}
