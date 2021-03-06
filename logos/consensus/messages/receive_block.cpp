#include <logos/consensus/messages/receive_block.hpp>

ReceiveBlock::ReceiveBlock(const BlockHash & previous,
                           const BlockHash & send_hash,
                           uint16_t index)
    : previous(previous)
    , source_hash(send_hash)
    , index(index)
{}

ReceiveBlock::ReceiveBlock(bool & error, const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *> (mdbval.data()), mdbval.size());

    error = logos::read(stream, previous);
    if(error)
    {
        return;
    }

    error = logos::read(stream, source_hash);
    if(error)
    {
        return;
    }

    error = logos::read(stream, index);
    if(error)
    {
        return;
    }
    index= le16toh(index);
}

std::string ReceiveBlock::ToJson() const
{
    boost::property_tree::ptree tree;
    SerializeJson (tree);
    std::stringstream ostream;
    boost::property_tree::write_json(ostream, tree);
    return ostream.str();
}

boost::property_tree::ptree ReceiveBlock::SerializeJson() const
{
    boost::property_tree::ptree tree;
    SerializeJson (tree);
    return tree;
}

void ReceiveBlock::SerializeJson(boost::property_tree::ptree & tree) const
{
    tree.put("previous", previous.to_string());
    tree.put("source_hash", source_hash.to_string());
    tree.put("index", std::to_string(index));
}

void ReceiveBlock::Serialize(logos::stream & stream) const
{
    uint16_t idx = htole16(index);

    logos::write(stream, previous);
    logos::write(stream, source_hash);
    logos::write(stream, idx);
}

BlockHash ReceiveBlock::Hash() const
{
    return Blake2bHash<ReceiveBlock>(*this);
}

void ReceiveBlock::Hash(blake2b_state & hash) const
{
    uint16_t s = htole16(index);
    source_hash.Hash(hash);
    blake2b_update(&hash, &s, sizeof(uint16_t));
}

logos::mdb_val ReceiveBlock::to_mdb_val(std::vector<uint8_t> &buf) const
{
    assert(buf.empty());
    {
        logos::vectorstream stream(buf);
        Serialize(stream);
    }
    return logos::mdb_val(buf.size(), buf.data());
}
