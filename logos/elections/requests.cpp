#include <logos/elections/requests.hpp>
#include <logos/request/fields.hpp>
#include <logos/lib/log.hpp>

AnnounceCandidacy::AnnounceCandidacy(
            const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence)
    : Request(
            RequestType::AnnounceCandidacy,
            origin, previous, fee, sequence)
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

AnnounceCandidacy::AnnounceCandidacy(bool& error, const logos::mdb_val& mdbval)
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



AnnounceCandidacy::AnnounceCandidacy(bool & error,
            boost::property_tree::ptree const & tree) : Request(error, tree)
{
    error = error || type != RequestType::AnnounceCandidacy;
    
    if(error)
    {
        std::cout << "error in base ctor" << std::endl;
        throw std::runtime_error("Error in base ctor when constructing AnnounceCandidacy request");
        return;
    }

    try
    {
        boost::optional<std::string> stake_text (tree.get_optional<std::string>("stake"));
        if(stake_text.is_initialized())
        {
            error = stake.decode_hex(stake_text.get());
            if(error)
            {
                std::cout << "error decoding stake" << std::endl;
                throw std::runtime_error("Error decoding stake");
                return;
            }
        }
        else
        {
            stake = 0;
        }

        std::string bls_key_text = tree.get<std::string>("bls_key");
        bls_key = DelegatePubKey(bls_key_text);

    }
    catch(std::exception& e)
    {
        error = true;
        throw e;
    }
    Hash();
}

uint64_t AnnounceCandidacy::Serialize(logos::stream & stream) const
{
    auto val = logos::write(stream, stake);
    val += logos::write(stream, bls_key);
    return val;
}

void AnnounceCandidacy::Deserialize(bool & error, logos::stream & stream)
{
    error = logos::read(stream, stake);
    if(error)
    {
        return;
    }
    error = logos::read(stream, bls_key);
}

void AnnounceCandidacy::DeserializeDB(bool & error, logos::stream & stream)
{
    Request::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

boost::property_tree::ptree AnnounceCandidacy::SerializeJson() const
{
    boost::property_tree::ptree tree(Request::SerializeJson());

    tree.put("stake",stake.to_string());
    tree.put("bls_key",bls_key.to_string());    
    return tree;
}


uint16_t AnnounceCandidacy::WireSize() const
{
    return Request::WireSize() + sizeof(stake) + sizeof(bls_key); 
}

RenounceCandidacy::RenounceCandidacy(
            const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence)
    : Request(
            RequestType::RenounceCandidacy,
            origin, previous, fee, sequence)
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

RenounceCandidacy::RenounceCandidacy(bool& error, const logos::mdb_val& mdbval)
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
        uint32_t sequence)
    : Request(
            RequestType::ElectionVote,
            origin, previous, fee, sequence)
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
        auto votes = tree.get_child("votes");
        for(const std::pair<std::string,boost::property_tree::ptree> &v : votes)
        {
            auto account_s (v.first);
            AccountAddress candidate;
            error = candidate.decode_account(account_s);
            std::cout << "account is : " << v.first << std::endl;
            if(error)
            {
                std::cout << "error decoding account" << std::endl;
            }
            uint8_t vote_val;
            vote_val = std::stoi(v.second.data());
            CandidateVotePair vp(candidate, vote_val);
            votes_.push_back(vp);
        }
        Hash();
    }
    catch(std::exception const & e)
    {
        error = true;
        throw e;
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
    
    boost::property_tree::ptree tree(Request::SerializeJson());

    tree.add_child(VOTES,votes_tree);
    return tree; 
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
            const Amount stake)
    : Request(RequestType::StartRepresenting, origin, previous, fee, sequence), stake(stake)
{
    Hash();
}

StartRepresenting::StartRepresenting(
        const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountSig & signature,
            const Amount stake)
    : Request(RequestType::StartRepresenting, origin, previous, fee, sequence, signature), stake(stake)
{
    Hash();
}


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

uint64_t StartRepresenting::Serialize(logos::stream & stream) const
{
    return logos::write(stream, stake);
}

void StartRepresenting::Deserialize(bool& error, logos::stream& stream)
{
    error = logos::read(stream, stake);
    if(error)
    {
        std::cout << "error reading stake" << std::endl;
    }
}

void StartRepresenting::DeserializeDB(bool& error, logos::stream& stream)
{
    Request::DeserializeDB(error, stream);
    if(error)
    {
        std::cout << "error in base" << std::endl;
        return;
    }

    Deserialize(error, stream);
}



boost::property_tree::ptree StartRepresenting::SerializeJson() const
{
    boost::property_tree::ptree tree = Request::SerializeJson();
    tree.put("stake",stake.to_string());
    return tree;
}

StopRepresenting::StopRepresenting(
        const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence)
    : Request(RequestType::StopRepresenting, origin, previous, fee, sequence)
{
    Hash();
}

StopRepresenting::StopRepresenting(
        const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountSig & signature)
    : Request(RequestType::StopRepresenting, origin, previous, fee, sequence, signature)
{
    Hash();
}


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
