#include <logos/consensus/messages/receive_block.hpp>

ReceiveBlock::ReceiveBlock(const BlockHash & previous,
                           const BlockHash & send_hash,
                           uint16_t index2send)
    : previous(previous)
    , send_hash(send_hash)
    , index2send(index2send)
{}

ReceiveBlock::ReceiveBlock(bool & error, const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *> (mdbval.data()), mdbval.size());

    error = logos::read(stream, previous);
    if(error)
    {
        return;
    }

    error = logos::read(stream, send_hash);
    if(error)
    {
        return;
    }

    error = logos::read(stream, index2send);
    if(error)
    {
        return;
    }
    index2send= le16toh(index2send);
}

//std::string ReceiveBlock::SerializeJson() const
//{
//    boost::property_tree::ptree tree;
//    SerializeJson (tree);
//    std::stringstream ostream;
//    boost::property_tree::write_json(ostream, tree);
//    return ostream.str();
//}

void ReceiveBlock::SerializeJson(boost::property_tree::ptree & tree) const
{
    tree.put("previous", previous.to_string());
    tree.put("send_hash", send_hash.to_string());
    tree.put("index_to_send_block", std::to_string(index2send));
}

boost::property_tree::ptree ReceiveBlock::SerializeJson() const
{
    boost::property_tree::ptree tree;
    SerializeJson(tree);
    return tree;
}

void ReceiveBlock::Serialize(logos::stream & stream) const
{
    uint16_t idx = htole16(index2send);

    logos::write(stream, previous);
    logos::write(stream, send_hash);
    logos::write(stream, idx);
}

BlockHash ReceiveBlock::Hash() const
{
    return Blake2bHash<ReceiveBlock>(*this);
}

void ReceiveBlock::Hash(blake2b_state & hash) const
{
    uint16_t s = htole16(index2send);
    send_hash.Hash(hash);
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
