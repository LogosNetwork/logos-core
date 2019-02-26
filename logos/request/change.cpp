#include <logos/request/change.hpp>

#include <logos/request/fields.hpp>

Change::Change()
    : Request(RequestType::Change)
{}

Change::Change(bool & error,
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

Change::Change(bool & error,
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

Change::Change(bool & error,
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
        error = client.decode_account(tree.get<std::string>(CLIENT));
        if(error)
        {
            return;
        }

        error = representative.decode_account(tree.get<std::string>(REPRESENTATIVE));

        if(error)
        {
            return;
        }

        Hash();
    }
    catch(...)
    {
        error = true;
    }
}

boost::property_tree::ptree Change::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree;

    tree.put(CLIENT, client.to_account());
    tree.put(REPRESENTATIVE, representative.to_account());

    return tree;
}

uint64_t Change::Serialize(logos::stream & stream) const
{
    return logos::write(stream, client) +
           logos::write(stream, representative);
}

void Change::Deserialize(bool & error, logos::stream & stream)
{
    error = logos::read(stream, client);
    if(error)
    {
        return;
    }

    error = logos::read(stream, representative);
}

void Change::DeserializeDB(bool &error, logos::stream &stream)
{
    Request::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

void Change::Hash(blake2b_state & hash) const
{
    client.Hash(hash);
    representative.Hash(hash);
}

uint16_t Change::WireSize() const
{
    return sizeof(client.bytes) +
           sizeof(representative.bytes) +
           Request::WireSize();
}

bool Change::operator==(const Request & other) const
{
    try
    {
        auto derived = dynamic_cast<const Change &>(other);

        return Request::operator==(other) &&
               client == derived.client &&
               representative == derived.representative;
    }
    catch(...)
    {}

    return false;
}
