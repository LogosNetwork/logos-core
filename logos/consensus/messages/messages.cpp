#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/messages/util.hpp>
#include <logos/common.hpp>

BatchStateBlock::BatchStateBlock(bool & error, logos::stream & stream, bool with_state_block)
    : PrePrepareCommon(error, stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, block_count);
    if(error || block_count > CONSENSUS_BATCH_SIZE)
    {
        return;
    }
    block_count = le16toh(block_count);

    for(uint64_t i = 0; i < block_count; ++i)
    {
        error = logos::read(stream, hashs[i]);
        if(error)
        {
            return;
        }
     }

    if( with_state_block )
    {
        for(uint64_t i = 0; i < block_count; ++i)
        {
            new(&blocks[i]) StateBlock(error, stream);
            if(error)
            {
                return;
            }
        }
    }
}

//TODO
//std::ostream& operator<<(std::ostream& os, const BatchStateBlock& b)
//{
//    os << static_cast<const PrePrepareCommon &>(b)
//
//       << " sequence: " << b.sequence
//       << " block_count: " << b.block_count
//       << " epoch_number: " << b.epoch_number
//       << " delegate: " << b.delegate
//       << " blocks: --"
//       << " signature: --";
//
//    return os;
//}


void BatchStateBlock::SerializeJson(boost::property_tree::ptree & batch_state_block) const
{
    PrePrepareCommon::SerializeJson(batch_state_block);
    batch_state_block.put("type", "BatchStateBlock");
    batch_state_block.put("block_count", std::to_string(block_count));

    boost::property_tree::ptree blocks_tree;
    for(uint64_t i = 0; i < block_count; ++i)
    {
        boost::property_tree::ptree txn_content;
        blocks[i].SerializeJson(txn_content, true, false);
        blocks_tree.push_back(std::make_pair("", txn_content));
    }
    batch_state_block.add_child("blocks", blocks_tree);
}

uint32_t BatchStateBlock::Serialize(logos::stream & stream, bool with_state_block) const
{
    uint16_t bc = htole16(block_count);

    auto s = PrePrepareCommon::Serialize(stream);
    s += logos::write(stream, bc);

    for(uint64_t i = 0; i < block_count; ++i)
    {
        s += logos::write(stream, hashs[i]);
    }

    if(with_state_block)
    {
        for(uint64_t i = 0; i < block_count; ++i)
        {
            s += blocks[i].Serialize(stream, false);
        }
    }

    return s;
}

const size_t ConnectedClientIds::STREAM_SIZE;

//TODO
//template<MessageType MT, ConsensusType CT>
//std::ostream& operator<<(std::ostream& os, const StandardPhaseMessage<MT, CT>& m)
//{
//    os << static_cast<const MessagePrequel<MT, CT> &>(m);
//    return os;
//}

