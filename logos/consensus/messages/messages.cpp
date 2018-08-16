#include <logos/consensus/messages/messages.hpp>
#include <logos/common.hpp>

const size_t BatchStateBlock::HASHABLE_BYTES = sizeof(BatchStateBlock)
                                               - sizeof(Signature);

BlockHash BatchStateBlock::Hash() const
{
    logos::uint256_union result;
    blake2b_state hash;

    auto status (blake2b_init (&hash, sizeof (result.bytes)));
    assert (status == 0);

    blake2b_update(&hash, &block_count, sizeof(block_count));
    blake2b_update(&hash, blocks, sizeof(BlockList));

    status = blake2b_final (&hash, result.bytes.data (), sizeof (result.bytes));
    assert (status == 0);

    return result;
}

std::string BatchStateBlock::SerializeJson() const
{
    boost::property_tree::ptree batch_state_block;

    batch_state_block.put("hash", Hash().to_string());

    boost::property_tree::ptree blocks_tree;

    for(uint64_t i = 0; i < block_count; ++i)
    {
        blocks_tree.push_back(std::make_pair("", blocks[i].serialize_json()));
    }

    batch_state_block.add_child("blocks", blocks_tree);

    std::stringstream ostream;
    boost::property_tree::write_json(ostream, batch_state_block);
    return ostream.str();
}
