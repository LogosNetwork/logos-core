#include <logos/request/change.hpp>

#include <logos/request/fields.hpp>

Change::Change(bool & error,
               std::basic_streambuf<uint8_t> & stream)
    : Request(error, stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, client);
    if(error)
    {
        return;
    }

    error = logos::read(stream, representative);
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
    return Request::Serialize(stream) +
           logos::write(stream, client) +
           logos::write(stream, representative);
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
