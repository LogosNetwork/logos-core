///
/// @file
/// This file contains definition of the Epoch
///
#include <logos/epoch/epoch.hpp>

const size_t Epoch::HASHABLE_BYTES = sizeof(Epoch)
                                            - sizeof(Signature);

std::string Epoch::SerializeJson() const
{
    boost::property_tree::ptree epoch_block;

    epoch_block.put("hash", Hash().to_string());
    // TODO: add more block content here

    std::stringstream ostream;
    boost::property_tree::write_json(ostream, epoch_block);
    return ostream.str();
}
