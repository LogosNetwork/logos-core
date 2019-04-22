#include <logos/staking/requests.hpp>

using namespace request::fields;

Proxy::Proxy()
    : Request(RequestType::Proxy)
{}  

Proxy::Proxy(bool & error,
            std::basic_streambuf<uint8_t> & stream) : Request(error, stream)
{
    //ensure type is correct
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
            boost::property_tree::ptree const & tree) : Request(error, tree)
{
    error = error || type != RequestType::Proxy;
    
    if(error)
    {
        return;
    }

    try
    {
        boost::optional<std::string> lock_proxy_text (
                tree.get_optional<std::string>(LOCK_PROXY));
        if(lock_proxy_text.is_initialized())
        {
            error = lock_proxy.decode_hex(lock_proxy_text.get());
        }
        else
        {
            lock_proxy = 0;
        }

        error = rep.decode_hex(tree.get<std::string>(REPRESENTATIVE));
        epoch_num = std::stol(tree.get<std::string>(EPOCH_NUM));
        error = staking_subchain_previous.decode_hex(tree.get<std::string>(STAKING_SUB_PREV));

    }
    catch(std::exception& e)
    {
        error = true;
    }
    SignAndHash(error, tree);
}

uint64_t Proxy::Serialize(logos::stream & stream) const
{
    auto val = logos::write(stream, lock_proxy);
    val += logos::write(stream, rep);
    val += logos::write(stream, epoch_num);
    val += logos::write(stream, staking_subchain_previous);
    val += logos::write(stream, signature);
    return val;
}

void Proxy::Deserialize(bool & error, logos::stream & stream)
{

    error = logos::read(stream, lock_proxy)
        || logos::read(stream, rep)
        || logos::read(stream, epoch_num)
        || logos::read(stream, staking_subchain_previous);

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
    Request::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

boost::property_tree::ptree Proxy::SerializeJson() const
{
    boost::property_tree::ptree tree(Request::SerializeJson());
    tree.put(LOCK_PROXY, lock_proxy.to_string());
    tree.put(REPRESENTATIVE, rep.to_string());
    tree.put(EPOCH_NUM, epoch_num);
    tree.put(STAKING_SUB_PREV, staking_subchain_previous.to_string());

    return tree;
}

void Proxy::Hash(blake2b_state& hash) const
{
    Request::Hash(hash);
    blake2b_update(&hash, &lock_proxy, sizeof(lock_proxy));
    blake2b_update(&hash, &rep, sizeof(rep));
    blake2b_update(&hash, &epoch_num, sizeof(epoch_num));
    blake2b_update(&hash, &staking_subchain_previous, sizeof(staking_subchain_previous));
}

bool Proxy::operator==(const Proxy& other) const
{
    return lock_proxy == other.lock_proxy
        && rep == other.rep
        && epoch_num == other.epoch_num
        && staking_subchain_previous == other.staking_subchain_previous 
        && Request::operator==(other);
}

