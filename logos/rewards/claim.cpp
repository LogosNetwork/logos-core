#include <logos/rewards/claim.hpp>

Claim::Claim()
    : Request(RequestType::Claim)
{}

Claim::Claim(bool & error,
             const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

Claim::Claim(bool & error,
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

Claim::Claim(bool & error,
             boost::property_tree::ptree const & tree)
    : Request(error, tree)
{
    using namespace request::fields;

    if(error)
    {
        return;
    }

    try
    {
        error = epoch_hash.decode_hex(tree.get<std::string>(EPOCH_HASH));
        if(error)
        {
            return;
        }

        epoch_number = std::stoul(tree.get<std::string>(EPOCH_NUMBER));
        if(error)
        {
            return;
        }

        SignAndHash(error, tree);
    }
    catch(...)
    {
        error = true;
    }
}

boost::property_tree::ptree Claim::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = Request::SerializeJson();

    tree.put(EPOCH_HASH, epoch_hash.to_string());
    tree.put(EPOCH_NUMBER, epoch_number);

    return tree;
}

uint64_t Claim::Serialize(logos::stream & stream) const
{
    return Request::Serialize(stream) +
           logos::write(stream, epoch_hash) +
           logos::write(stream, epoch_number);
}

void Claim::Deserialize(bool & error, logos::stream & stream)
{
    error = logos::read(stream, epoch_hash);
    if(error)
    {
        return;
    }

    error = logos::read(stream, epoch_number);
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
        if(error)
        {
            return;
        }
    }
}

void Claim::DeserializeDB(bool & error, logos::stream & stream)
{
    Request::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    error = logos::read(stream, next);
}

void Claim::Hash(blake2b_state & hash) const
{
    Request::Hash(hash);

    epoch_hash.Hash(hash);
    blake2b_update(&hash, &epoch_number, sizeof(epoch_number));
}

bool Claim::operator==(const Request & other) const
{
    try
    {
        auto derived = dynamic_cast<const Claim &>(other);

        return Request::operator==(other) &&
               epoch_hash == derived.epoch_hash &&
               epoch_number == derived.epoch_number;
    }
    catch(...)
    {}

    return false;
}
