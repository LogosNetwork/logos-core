///
/// @file
/// This file contains definition of the Epoch
///
#include <logos/epoch/epoch.hpp>

std::string Epoch::ToJson() const
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
    epoch_block.put("micro_block_tip", micro_block_tip.digest.to_string());

    boost::property_tree::ptree ptree_delegates;
    for (const auto & delegate : delegates) {
        boost::property_tree::ptree tip_member;
        delegate.SerializeJson(tip_member);
        ptree_delegates.push_back(std::make_pair("", tip_member));
    }
    epoch_block.add_child("delegates", ptree_delegates);
    epoch_block.put("transaction_fee_pool", transaction_fee_pool.to_string());
    epoch_block.put("total_supply", total_supply.to_string());
    epoch_block.put("is_extension", is_extension);
}
