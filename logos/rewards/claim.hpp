#pragma once

#include <logos/request/requests.hpp>

struct Claim : public Request
{
    using Request::Hash;

    Claim();

    Claim(bool & error,
          const logos::mdb_val & mdbval);

    Claim(bool & error,
          std::basic_streambuf<uint8_t> & stream);

    Claim(bool & error,
          boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    bool operator==(const Request & other) const override;

    BlockHash epoch_hash;
    uint32_t  epoch_number;
};
