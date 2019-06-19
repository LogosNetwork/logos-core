#include <logos/governance/requests.hpp>

using namespace request::fields;

bool DeserializeStakeJson(boost::property_tree::ptree const & tree,
                          Amount & stake,
                          bool & set_stake)
{
    auto stake_text(tree.get_optional<std::string>(STAKE));
    auto set_stake_text(tree.get_optional<std::string>(SET_STAKE));

    if(set_stake_text.is_initialized())
    {
        set_stake = set_stake_text.get() == "true";
    }
    else
    {
        set_stake = stake_text.is_initialized();
    }

    if(stake_text.is_initialized())
    {
        return stake.decode_dec(stake_text.get());
    }

    stake = 0;
    return set_stake;
}

Governance::Governance(RequestType type)
    : Request(type)
    , governance_subchain_prev(0)
{}

Governance::Governance(bool & error,
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

Governance::Governance(bool& error, const logos::mdb_val& mdbval)
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

Governance::Governance(bool & error,
                       boost::property_tree::ptree const & tree)
    : Request(error, tree)
{
    if(error)
    {
        return;
    }

    try
    {
        epoch_num = std::stol(tree.get<std::string>(EPOCH_NUM));

        error |= governance_subchain_prev.decode_hex(tree.get<std::string>(GOV_SUB_PREV));

        if(error)
        {
            return;
        }
    }
    catch(std::exception& e)
    {
        Log log;
        LOG_ERROR(log) << "Governance::Governance(error, ptree) - "
                       << "exception in constructor. e.what() = " << e.what();
        error = true;
        return;
    }

    SignAndHash(error, tree);
}

uint64_t Governance::Serialize(logos::stream & stream) const
{
    auto val = logos::write(stream, epoch_num);
    val += logos::write(stream, governance_subchain_prev);

    return val;
}

void Governance::Deserialize(bool & error, logos::stream & stream)
{
    error = logos::read(stream, epoch_num)
            || logos::read(stream, governance_subchain_prev);
}

void Governance::DeserializeDB(bool & error, logos::stream & stream)
{
    Request::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

boost::property_tree::ptree Governance::SerializeJson() const
{
    boost::property_tree::ptree tree(Request::SerializeJson());

    tree.put(EPOCH_NUM, epoch_num);
    tree.put(GOV_SUB_PREV, governance_subchain_prev.to_string());

    return tree;
}

void Governance::Hash(blake2b_state& hash) const
{
    Request::Hash(hash);
    blake2b_update(&hash, &epoch_num, sizeof(epoch_num));
    blake2b_update(&hash, &governance_subchain_prev, sizeof(governance_subchain_prev));
}

bool Governance::operator==(const Governance& other) const
{
    return epoch_num == other.epoch_num
           && governance_subchain_prev == other.governance_subchain_prev
           && Request::operator==(other);
}

Proxy::Proxy()
    : Governance(RequestType::Proxy)
{}

Proxy::Proxy(bool & error,
             std::basic_streambuf<uint8_t> & stream)
    : Governance(error, stream)
{
    error = error || type != RequestType::Proxy;

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

Proxy::Proxy(bool& error, const logos::mdb_val& mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *> (mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);

    error = error || type != RequestType::Proxy;
    if(error)
    {
        return;
    }

    Hash();
}

Proxy::Proxy(bool & error,
             boost::property_tree::ptree const & tree)
     : Governance(error, tree)
{
    error |= type != RequestType::Proxy;

    if(error)
    {
        return;
    }

    try
    {
        error = lock_proxy.decode_dec(tree.get<std::string>(LOCK_PROXY, "0"));
        if(error)
        {
            return;
        }

        error |= rep.decode_account(tree.get<std::string>(REPRESENTATIVE));
        if(error)
        {
            return;
        }
    }
    catch(std::exception& e)
    {
        Log log;
        LOG_ERROR(log) << "Proxy::Proxy(error, ptree) - "
                       << "exception in constructor. e.what() = " << e.what();
        error = true;
        return;
    }

    SignAndHash(error, tree);
}

uint64_t Proxy::Serialize(logos::stream & stream) const
{
    auto val = Governance::Serialize(stream);

    val += logos::write(stream, lock_proxy);
    val += logos::write(stream, rep);
    val += logos::write(stream, signature);

    return val;
}

void Proxy::Deserialize(bool & error, logos::stream & stream)
{

    error = logos::read(stream, lock_proxy)
            || logos::read(stream, rep);

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
    }
}

void Proxy::DeserializeDB(bool & error, logos::stream & stream)
{
    Governance::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

boost::property_tree::ptree Proxy::SerializeJson() const
{
    boost::property_tree::ptree tree(Governance::SerializeJson());

    tree.put(LOCK_PROXY, lock_proxy.to_string_dec());
    tree.put(REPRESENTATIVE, rep.to_account());

    return tree;
}

void Proxy::Hash(blake2b_state& hash) const
{
    Governance::Hash(hash);
    blake2b_update(&hash, &lock_proxy, sizeof(lock_proxy));
    blake2b_update(&hash, &rep, sizeof(rep));
}

bool Proxy::operator==(const Proxy& other) const
{
    return lock_proxy == other.lock_proxy
           && rep == other.rep
           && Governance::operator==(other);
}

Stake::Stake()
    : Governance(RequestType::Stake)
{}

Stake::Stake(bool & error,
             std::basic_streambuf<uint8_t> & stream)
    : Governance(error, stream)
{
    error = error || type != RequestType::Stake;

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

Stake::Stake(bool& error, const logos::mdb_val& mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *> (mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);

    error = error || type != RequestType::Stake;
    if(error)
    {
        return;
    }

    Hash();
}

Stake::Stake(bool & error,
             boost::property_tree::ptree const & tree)
    : Governance(error, tree)
{
    error |= type != RequestType::Stake;

    if(error)
    {
        return;
    }

    try
    {
        error = stake.decode_dec(tree.get<std::string>(STAKE));
        if(error)
        {
            return;
        }
    }
    catch(std::exception& e)
    {
        Log log;
        LOG_ERROR(log) << "Stake::Stake(error, ptree) - "
                       << "exception in constructor. e.what() = " << e.what();
        error = true;
        return;
    }

    SignAndHash(error, tree);
}

uint64_t Stake::Serialize(logos::stream & stream) const
{
    auto val = Governance::Serialize(stream);

    val += logos::write(stream, stake);
    val += logos::write(stream, signature);

    return val;
}

void Stake::Deserialize(bool & error, logos::stream & stream)
{
    error = logos::read(stream, stake);
    if(error)
    {
        return;
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
    }

}

void Stake::DeserializeDB(bool & error, logos::stream & stream)
{
    Governance::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

boost::property_tree::ptree Stake::SerializeJson() const
{
    boost::property_tree::ptree tree(Governance::SerializeJson());

    tree.put(STAKE, stake.to_string_dec());

    return tree;
}

void Stake::Hash(blake2b_state& hash) const
{
    Governance::Hash(hash);
    blake2b_update(&hash, &stake, sizeof(stake));
}

bool Stake::operator==(const Stake& other) const
{
    return stake == other.stake
           && Governance::operator==(other);
}

Unstake::Unstake()
    : Governance(RequestType::Unstake)
{}

Unstake::Unstake(bool & error,
                 std::basic_streambuf<uint8_t> & stream)
    : Governance(error, stream)
{
    //ensure type is correct
    error = error || type != RequestType::Unstake;

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

Unstake::Unstake(bool& error, const logos::mdb_val& mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *> (mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);

    error = error || type != RequestType::Unstake;
    if(error)
    {
        return;
    }

    Hash();
}

Unstake::Unstake(bool & error,
                 boost::property_tree::ptree const & tree)
    : Governance(error, tree)
{
    error |= type != RequestType::Unstake;

    if(error)
    {
        return;
    }

    SignAndHash(error, tree);
}

uint64_t Unstake::Serialize(logos::stream & stream) const
{
    auto val = Governance::Serialize(stream);
    val += logos::write(stream, signature);

    return val;
}

void Unstake::Deserialize(bool & error, logos::stream & stream)
{
    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
    }

}

void Unstake::DeserializeDB(bool & error, logos::stream & stream)
{
    Governance::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

bool Unstake::operator==(const Unstake& other) const
{
    return Governance::operator==(other);
}

using CandidateVotePair = ElectionVote::CandidateVotePair;

CandidateVotePair::CandidateVotePair(const std::string & account,
                                     uint8_t num_votes)
    : num_votes(num_votes)
{
    this->account.decode_account(account);
}

CandidateVotePair::CandidateVotePair(const AccountAddress & account,
                                     uint8_t num_votes)
    : account(account)
    , num_votes(num_votes)
{}

CandidateVotePair::CandidateVotePair(bool & error,
                                     boost::property_tree::ptree const & tree)
{
    DeserializeJson(error, tree);
}

CandidateVotePair::CandidateVotePair(bool & error,
                                     logos::stream & stream)
{
    Deserialize(error, stream);
}

void CandidateVotePair::DeserializeJson(bool & error,
                                        boost::property_tree::ptree const & tree)
{
    using namespace request::fields;

    try
    {
        error = account.decode_account(tree.get<std::string>(ACCOUNT));
        if(error)
        {
            return;
        }

        num_votes = std::stoul(tree.get<std::string>(NUM_VOTES));
    }
    catch(...)
    {
        error = true;
    }
}

boost::property_tree::ptree CandidateVotePair::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree;

    tree.put(ACCOUNT, account.to_account());
    tree.put(NUM_VOTES, std::to_string(num_votes));

    return tree;
}

uint64_t CandidateVotePair::Serialize(logos::stream& stream) const
{
    return logos::write(stream, account) +
           logos::write(stream, num_votes);
}

void CandidateVotePair::Deserialize(bool & error, logos::stream & stream)
{
    error = logos::read(stream, account);
    if(error)
    {
        return;
    }

    error = logos::read(stream, num_votes);
}

uint64_t CandidateVotePair::WireSize()
{
    return sizeof(account) + sizeof(num_votes);
}

bool CandidateVotePair::operator==(const CandidateVotePair& other) const
{
    return account == other.account && num_votes == other.num_votes;
}

ElectionVote::ElectionVote()
    : Governance(RequestType::ElectionVote)
{}

ElectionVote::ElectionVote(bool & error,
                           std::basic_streambuf<uint8_t> & stream)
    : Governance(error, stream)
{
    error = error || type != RequestType::ElectionVote;
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
    : Governance(error, tree)
{
    error = error || type != RequestType::ElectionVote;
    if(error)
    {
        return;
    }

    try
    {
        auto votes_tree = tree.get_child(VOTES);
        for(const auto & entry : votes_tree)
        {
            CandidateVotePair c(error, entry.second);
            if(error)
            {
                return;
            }
            votes.push_back(c);
        }

        SignAndHash(error, tree);
    }
    catch(...)
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

    error = error || type != RequestType::ElectionVote;
    if(error)
    {
        return;
    }

    Hash();
}

void ElectionVote::Hash(blake2b_state& hash) const
{
    Governance::Hash(hash);

    for(const auto & v : votes)
    {
        v.account.Hash(hash);
        blake2b_update(&hash, &(v.num_votes),sizeof(v.num_votes));
    }
}

boost::property_tree::ptree ElectionVote::SerializeJson() const
{
    boost::property_tree::ptree tree(Governance::SerializeJson());

    boost::property_tree::ptree votes_tree;
    for(size_t i = 0; i < votes.size(); ++i)
    {
        votes_tree.push_back(std::make_pair("",
                                            votes[i].SerializeJson()));
    }
    tree.add_child(VOTES, votes_tree);

    return tree;
}

uint64_t ElectionVote::Serialize(logos::stream & stream) const
{
    uint8_t count = votes.size(); //TODO: this is not safe if size is too big
    uint64_t val = Governance::Serialize(stream);
    val += logos::write(stream, count);

    for(auto const & v : votes)
    {
        val += v.Serialize(stream);
    }

    val += logos::write(stream, signature);

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
        votes.push_back(vote);
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
    }
}

void ElectionVote::DeserializeDB(bool & error, logos::stream & stream)
{
    Governance::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

bool ElectionVote::operator==(const ElectionVote& other) const
{
    return votes == other.votes
           && Governance::operator==(other);
}

bool ElectionVote::operator!=(const ElectionVote& other) const
{
    return !(*this == other);
}

AnnounceCandidacy::AnnounceCandidacy()
    : Governance(RequestType::AnnounceCandidacy)
    , set_stake(false)
    , levy_percentage(100)
{}

AnnounceCandidacy::AnnounceCandidacy(bool & error,
                                     std::basic_streambuf<uint8_t> & stream)
    : Governance(error, stream)
{
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

    error = type != RequestType::AnnounceCandidacy;
    if(error)
    {
        return;
    }

    Hash();
}

AnnounceCandidacy::AnnounceCandidacy(bool & error,
                                     boost::property_tree::ptree const & tree)
    : Governance(error, tree)
{
    error = error || type != RequestType::AnnounceCandidacy;

    if(error)
    {
        return;
    }

    try
    {
        error = DeserializeStakeJson(tree, stake, set_stake);
        if(error)
        {
            return;
        }

        std::string bls_key_text = tree.get<std::string>(BLS_KEY);
        bls_key = DelegatePubKey(bls_key_text);
        ecies_key.DeserializeJson(tree);
        levy_percentage = std::stol(tree.get<std::string>(LEVY_PERCENTAGE));
    }
    catch(std::exception& e)
    {
        error = true;
        return;
    }

    SignAndHash(error, tree);
}

uint64_t AnnounceCandidacy::Serialize(logos::stream & stream) const
{
    auto val = Governance::Serialize(stream);

    val += logos::write(stream, set_stake);

    if(set_stake)
    {
        val += logos::write(stream, stake);
    }

    val += logos::write(stream, bls_key);
    val += ecies_key.Serialize(stream);
    val += logos::write(stream, levy_percentage);
    val += logos::write(stream, signature);

    return val;
}

void AnnounceCandidacy::Deserialize(bool & error, logos::stream & stream)
{
    error = logos::read(stream, set_stake);
    if(error)
    {
        return;
    }

    if(set_stake)
    {
        error = logos::read(stream, stake);
        if(error)
        {
            return;
        }
    }

    error = logos::read(stream, bls_key);
    if(error)
    {
        return;
    }

    error = ecies_key.Deserialize(stream);
    if (error)
    {
        return;
    }

    error = logos::read(stream, levy_percentage);
    if(error)
    {
        return;
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
    }
}

void AnnounceCandidacy::DeserializeDB(bool & error, logos::stream & stream)
{
    Governance::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

boost::property_tree::ptree AnnounceCandidacy::SerializeJson() const
{
    boost::property_tree::ptree tree(Governance::SerializeJson());

    tree.put(SET_STAKE, set_stake);
    tree.put(STAKE, stake.to_string_dec());
    tree.put(BLS_KEY, bls_key.to_string());

    ecies_key.SerializeJson(tree);

    tree.put(LEVY_PERCENTAGE, levy_percentage);

    return tree;
}

bool AnnounceCandidacy::operator==(const AnnounceCandidacy& other) const
{
    return stake == other.stake
           && bls_key == other.bls_key
           && ecies_key == other.ecies_key
           && levy_percentage == other.levy_percentage
           && Governance::operator==(other);
}

void AnnounceCandidacy::Hash(blake2b_state& hash) const
{
    Governance::Hash(hash);
    blake2b_update(&hash, &stake, sizeof(stake));
    bls_key.Hash(hash);
    ecies_key.Hash(hash);
    blake2b_update(&hash, &levy_percentage, sizeof(levy_percentage));
}

RenounceCandidacy::RenounceCandidacy()
    : Governance(RequestType::RenounceCandidacy)
    , set_stake(false)
{}

RenounceCandidacy::RenounceCandidacy(bool & error,
                                     std::basic_streambuf<uint8_t> & stream)
    : Governance(error, stream)
{
    error = error || type != RequestType::RenounceCandidacy;
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

RenounceCandidacy::RenounceCandidacy(bool& error, const logos::mdb_val& mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *> (mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);

    error = error || type != RequestType::RenounceCandidacy;
    if(error)
    {
        return;
    }

    Hash();
}

RenounceCandidacy::RenounceCandidacy(bool & error,
                                     boost::property_tree::ptree const & tree)
    : Governance(error, tree)
{
    error = error || type != RequestType::RenounceCandidacy;
    if(error)
    {
        return;
    }

    try
    {
        error = DeserializeStakeJson(tree, stake, set_stake);
    }
    catch(...)
    {
        error = true;
        return;
    }

    SignAndHash(error, tree);
}

uint64_t RenounceCandidacy::Serialize(logos::stream & stream) const
{
    auto val = Governance::Serialize(stream);
    val += logos::write(stream, set_stake);

    if(set_stake)
    {
        val += logos::write(stream, stake);
    }

    val += logos::write(stream, signature);
    return val;
}

void RenounceCandidacy::Deserialize(bool & error, logos::stream & stream)
{
    error = logos::read(stream, set_stake);
    if(error)
    {
        return;
    }

    if(set_stake)
    {
        error = logos::read(stream, stake);
        if(error)
        {
            return;
        }
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
    }
}

void RenounceCandidacy::DeserializeDB(bool & error, logos::stream & stream)
{
    Governance::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

boost::property_tree::ptree RenounceCandidacy::SerializeJson() const
{
    boost::property_tree::ptree tree(Governance::SerializeJson());

    tree.put(SET_STAKE,set_stake);
    tree.put(STAKE, stake.to_string());

    return tree;
}

bool RenounceCandidacy::operator==(const RenounceCandidacy& other) const
{
    return Governance::operator==(other);
}

StartRepresenting::StartRepresenting()
    : Governance(RequestType::StartRepresenting)
    , set_stake(false)
    , levy_percentage(100)
{}

StartRepresenting::StartRepresenting(bool & error,
                                     std::basic_streambuf<uint8_t> & stream)
    : Governance(error, stream)
{
    error = error || type != RequestType::StartRepresenting;
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

    error = error || type != RequestType::StartRepresenting;
    if(error)
    {
        return;
    }

    Hash();
}

StartRepresenting::StartRepresenting(bool & error,
                                     boost::property_tree::ptree const & tree)
    : Governance(error, tree)
{
    error = error || type != RequestType::StartRepresenting;
    if(error)
    {
        return;
    }

    try
    {
        error = DeserializeStakeJson(tree, stake, set_stake);
        levy_percentage = std::stol(tree.get<std::string>(LEVY_PERCENTAGE));
        SignAndHash(error, tree);
    }
    catch(...)
    {
        error = true;
    }
}

uint64_t StartRepresenting::Serialize(logos::stream & stream) const
{
    auto val = Governance::Serialize(stream);
    val += logos::write(stream, set_stake);

    if(set_stake)
    {
        val += logos::write(stream, stake);
    }

    val += logos::write(stream, levy_percentage);
    val += logos::write(stream, signature);

    return val;
}

void StartRepresenting::Deserialize(bool& error, logos::stream& stream)
{
    error = logos::read(stream, set_stake);
    if(error)
    {
        return;
    }

    if(set_stake)
    {
        error = logos::read(stream, stake);
        if(error)
        {
            return;
        }
    }

    error = logos::read(stream, levy_percentage);
    if(error)
    {
        return;
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
    }
}

void StartRepresenting::DeserializeDB(bool& error, logos::stream& stream)
{
    Governance::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

boost::property_tree::ptree StartRepresenting::SerializeJson() const
{
    boost::property_tree::ptree tree = Governance::SerializeJson();

    tree.put(SET_STAKE, set_stake);
    tree.put(STAKE, stake.to_string_dec());
    tree.put(LEVY_PERCENTAGE, levy_percentage);

    return tree;
}

bool StartRepresenting::operator==(const StartRepresenting& other) const
{
    return stake == other.stake
           && levy_percentage == other.levy_percentage
           && Governance::operator==(other);
}

void StartRepresenting::Hash(blake2b_state& hash) const
{
    Governance::Hash(hash);

    blake2b_update(&hash, &stake, sizeof(stake));
    blake2b_update(&hash, &levy_percentage, sizeof(levy_percentage));
}

StopRepresenting::StopRepresenting()
    : Governance(RequestType::StopRepresenting)
    , set_stake(false)
{}

StopRepresenting::StopRepresenting(bool & error,
                                   std::basic_streambuf<uint8_t> & stream)
    : Governance(error, stream)
{
    error = error || type != RequestType::StopRepresenting;
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

    error = error || type != RequestType::StopRepresenting;
    if(error)
    {
        return;
    }

    Hash();
}

StopRepresenting::StopRepresenting(bool & error,
                                   boost::property_tree::ptree const & tree)
    : Governance(error, tree)
{
    error = error || type != RequestType::StopRepresenting;
    if(error)
    {
        return;
    }

    try
    {
        error = DeserializeStakeJson(tree, stake, set_stake);
        SignAndHash(error, tree);
    }
    catch(...)
    {
        error = true;
    }
}

boost::property_tree::ptree StopRepresenting::SerializeJson() const
{
    boost::property_tree::ptree tree = Governance::SerializeJson();

    tree.put(SET_STAKE, set_stake);
    tree.put(STAKE, stake.to_string_dec());

    return tree;
}

void StopRepresenting::Deserialize(bool& error, logos::stream& stream)
{
    error = logos::read(stream, set_stake);
    if(error)
    {
        return;
    }

    if(set_stake)
    {
        error = logos::read(stream, stake);
        if(error)
        {
            return;
        }
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
    }
}

void StopRepresenting::DeserializeDB(bool& error, logos::stream& stream)
{
    Governance::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

uint64_t StopRepresenting::Serialize(logos::stream & stream) const
{
    auto val = Governance::Serialize(stream);
    val += logos::write(stream, set_stake);

    if(set_stake)
    {
        val += logos::write(stream, stake);
    }

    val += logos::write(stream, signature);
    return val;
}

bool StopRepresenting::operator==(const StopRepresenting& other) const
{
    return Governance::operator==(other);
}
