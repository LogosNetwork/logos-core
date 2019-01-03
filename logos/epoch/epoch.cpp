///
/// @file
/// This file contains definition of the Epoch
///
#include <logos/epoch/epoch.hpp>

const size_t Epoch::HASHABLE_BYTES = sizeof(Epoch)
                                            - sizeof(BlockHash)
                                            - sizeof(Signature);
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
    epoch_block.put("timestamp", std::to_string(timestamp));
    epoch_block.put("previous", previous.to_string());
    epoch_block.put("hash", Hash().to_string());
    epoch_block.put("account", account.to_string());
    epoch_block.put("epoch_number", std::to_string(epoch_number));
    epoch_block.put("micro_block_tip", micro_block_tip.to_string());
    boost::property_tree::ptree ptree_delegates;
    for (const auto & delegate : delegates) {
        boost::property_tree::ptree tip_member;
        tip_member.put("account", delegate.account.to_string());
        tip_member.put("vote", std::to_string(delegate.vote));
        tip_member.put("stake", std::to_string(delegate.stake));
        ptree_delegates.push_back(std::make_pair("", tip_member));
    }
    epoch_block.add_child("delegates", ptree_delegates);
    epoch_block.put("transaction_fee_pool", std::to_string(transaction_fee_pool));
    epoch_block.put("next", next.to_string());
    logos::uint256_union signature_tmp; // hacky fix, need to replicate uint256_union functionalities
    signature_tmp.bytes = signature;
    epoch_block.put("signature", signature_tmp.to_string ());
}
