/// @file
/// This file contains the definition of the MicroBlock classe, which is used
/// in the Microblock processing
#include <logos/microblock/microblock.hpp>

std::string MicroBlock::ToJson() const
{
    boost::property_tree::ptree micro_block;
    SerializeJson (micro_block);
    std::stringstream ostream;
    boost::property_tree::write_json(ostream, micro_block);
    return ostream.str();
}

void MicroBlock::SerializeJson(boost::property_tree::ptree & micro_block) const
{
    PrePrepareCommon::SerializeJson(micro_block);
    micro_block.put("type", "MicroBlock");
    micro_block.put("last_micro_block", std::to_string(last_micro_block));

    boost::property_tree::ptree ptree_tips;
    for (const auto & tip : tips) {
        boost::property_tree::ptree tip_member;
        tip_member.put("", tip.to_string());
        ptree_tips.push_back(std::make_pair("", tip_member));
    }
    micro_block.add_child("tips", ptree_tips);
    micro_block.put("number_batch_blocks", std::to_string(number_batch_blocks));
}

uint32_t MicroBlock::Serialize(logos::stream & stream, bool with_appendix) const
{
    auto s = PrePrepareCommon::Serialize(stream);
    s += logos::write(stream, last_micro_block);
    s += logos::write(stream, htole32(number_batch_blocks));

    for(int i = 0; i < NUM_DELEGATES; ++i)
    {
        s += logos::write(stream, tips[i]);
    }

    return s;
}
