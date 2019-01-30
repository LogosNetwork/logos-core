///
/// @file
/// This file contains definition of the Epoch
///
#include <logos/epoch/epoch.hpp>

std::string Epoch::SerializeJson() const
{
    boost::property_tree::ptree epoch_block;
    SerializeJson(epoch_block);
    std::stringstream ostream;
    boost::property_tree::write_json(ostream, epoch_block);
    return ostream.str();
}

void Epoch::SerializeJson(boost::property_tree::ptree & epoch_block) const
{
    PrePrepareCommon::SerializeJson(epoch_block);
    epoch_block.put("type", "Epoch");
    epoch_block.put("micro_block_tip", micro_block_tip.to_string());

    boost::property_tree::ptree ptree_delegates;
    for (size_t i = 0; i < NUM_DELEGATES; ++i) {
        boost::property_tree::ptree tip_member;
        delegates[i].SerializeJson(tip_member);
        std::string is_de = delegate_elects[i] == 1 ? "elect" : "persistent";
        ptree_delegates.push_back(std::make_pair(is_de, tip_member));
    }
    epoch_block.add_child("delegates", ptree_delegates);
    epoch_block.put("transaction_fee_pool", transaction_fee_pool.to_string());
}
