#pragma once

#include <logos/request/request.hpp>

struct Change : Request
{
    Change(bool & error,
           std::basic_streambuf<uint8_t> & stream);

    Change(bool & error,
           boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    AccountAddress client;
    AccountAddress representative;
};
