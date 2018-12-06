#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/messages/util.hpp>
#include <logos/common.hpp>

BatchStateBlock::BatchStateBlock(bool & error, logos::stream & stream)
    : Header(error, stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, sequence);
    if(error)
    {
        return;
    }

    error = logos::read(stream, block_count);
    if(error)
    {
        return;
    }

    error = logos::read(stream, epoch_number);
    if(error)
    {
        return;
    }

    for(uint64_t i = 0; i < block_count; ++i)
    {
        new(blocks + i) logos::state_block(error, stream);
        if(error)
        {
            return;
        }
    }

    error = logos::read(stream, next);
    if(error)
    {
        return;
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }
}

BlockHash BatchStateBlock::Hash() const
{
    logos::uint256_union result;
    blake2b_state hash;

    auto status(blake2b_init(&hash, sizeof(result.bytes)));
    assert(status == 0);

    blake2b_update(&hash, &sequence, sizeof(sequence));
    blake2b_update(&hash, &block_count, sizeof(block_count));
    blake2b_update(&hash, &epoch_number, sizeof(epoch_number));
    blake2b_update(&hash, &delegate_id, sizeof(delegate_id));

    for(uint64_t i = 0; i < block_count; ++i)
    {
        auto h = blocks[i].hash();
        blake2b_update(&hash, &h, sizeof(BlockHash));
    }

    status = blake2b_final(&hash, result.bytes.data(), sizeof(result.bytes));
    assert(status == 0);

    return result;
}

std::ostream& operator<<(std::ostream& os, const BatchStateBlock& b)
{
    os << static_cast<const MessageHeader<MessageType::Pre_Prepare,
                                          ConsensusType::BatchStateBlock>
                      &>(b)

       << " sequence: " << b.sequence
       << " block_count: " << b.block_count
       << " epoch_number: " << b.epoch_number
       << " blocks: --"
       << " next: " << b.next.to_string()
       << " signature: --";

    return os;
}

std::string BatchStateBlock::SerializeJson() const
{
    boost::property_tree::ptree batch_state_block;
    SerializeJson (batch_state_block);
    std::stringstream ostream;
    boost::property_tree::write_json(ostream, batch_state_block);
    return ostream.str();
}

void BatchStateBlock::SerializeJson(boost::property_tree::ptree & batch_state_block) const
{
    batch_state_block.put("timestamp", std::to_string(timestamp));
    batch_state_block.put("previous", previous.to_string());
    batch_state_block.put("hash", Hash().to_string());
    batch_state_block.put("block_count", std::to_string(block_count));
    batch_state_block.put("epoch_number", std::to_string(epoch_number));
    batch_state_block.put("delegate_id", std::to_string(delegate_id));
    logos::uint256_union signature_tmp; // hacky fix, need to replicate uint256_union functionalities
    signature_tmp.bytes = signature;
    batch_state_block.put("signature", signature_tmp.to_string ());

    boost::property_tree::ptree blocks_tree;

    for(uint64_t i = 0; i < block_count; ++i)
    {
        boost::property_tree::ptree txn_content;
        txn_content = blocks[i].serialize_json();
        txn_content.put("hash", blocks[i].hash().to_string());
        blocks_tree.push_back(std::make_pair("", txn_content));
    }

    batch_state_block.add_child("blocks", blocks_tree);
}

void BatchStateBlock::Serialize(logos::stream & stream) const
{
    auto & pss = const_cast<BatchStateBlock *>(this)->payload_stream_size;

    pss = BatchStateBlock::STREAM_SIZE +
          Header::STREAM_SIZE +
          Prequel::STREAM_SIZE +
          (logos::state_block::size * block_count);

    Header::Serialize(stream);

    logos::write(stream, sequence);
    logos::write(stream, block_count);
    logos::write(stream, epoch_number);

    for(uint64_t i = 0; i < block_count; ++i)
    {
        blocks[i].serialize(stream);
    }

    logos::write(stream, next);
    logos::write(stream, signature);
}

template<MessageType MT, ConsensusType CT>
std::ostream& operator<<(std::ostream& os, const StandardPhaseMessage<MT, CT>& m)
{
    os << static_cast<const MessageHeader<MT, CT> &>(m);
    return os;
}
