/// @file
/// This file contains the definition of the MicroBlock classe, which is used
/// in the Microblock processing
#include <logos/microblock/microblock.hpp>

const size_t MicroBlock::HASHABLE_BYTES = sizeof(MicroBlock)
                                            - sizeof(Signature);

BlockHash
MicroBlock::Hash() const {
    return merkle::Hash([&](std::function<void(const void *data, size_t)> cb)mutable -> void {
        //cb(&timestamp, sizeof(timestamp));
        cb(previous.bytes.data(), sizeof(previous));
        //cb(_merkle_root.bytes.data(), sizeof(_merkle_root));
        //cb(&_delegate, sizeof(_delegate));
        cb(&_epoch_number, sizeof(_epoch_number));
        cb(&_micro_block_number, sizeof(_micro_block_number));
        cb(_tips, NUM_DELEGATES * sizeof(BlockHash));
        cb(&_number_batch_blocks, sizeof(_number_batch_blocks));
    });
}

std::string MicroBlock::SerializeJson() const
{
    boost::property_tree::ptree micro_block;

    micro_block.put("timestamp", std::to_string(timestamp));
    micro_block.put("previous", previous.to_string());
    micro_block.put("hash", Hash().to_string());
    micro_block.put("delegate", _delegate.to_string());
    micro_block.put("epoch_number", std::to_string(_epoch_number));
    micro_block.put("micro_block_number", std::to_string(_micro_block_number));
    micro_block.put("last_micro_block", std::to_string(_last_micro_block));
    boost::property_tree::ptree tips;
    for (const auto & tip : _tips) {
        boost::property_tree::ptree tip_member;
        tip_member.put("", tip.to_string());
        tips.push_back(std::make_pair("", tip_member));
    }
    micro_block.add_child("tips", tips);
    micro_block.put("number_batch_blocks", std::to_string(_number_batch_blocks));
    logos::uint256_union signature_tmp; // hacky fix, need to replicate uint256_union functionalities
    signature_tmp.bytes = signature;
    micro_block.put("signature", signature_tmp.to_string ());

    std::stringstream ostream;
    boost::property_tree::write_json(ostream, micro_block);
    return ostream.str();
}
