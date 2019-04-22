#pragma once

#include <logos/request/requests.hpp>


struct Proxy : Request
{

    Proxy();

    Proxy(bool & error,
            std::basic_streambuf<uint8_t> & stream);

    Proxy(bool & error,
            boost::property_tree::ptree const & tree);

    Proxy(bool& error,
            const logos::mdb_val& mdbval);


    void Deserialize(bool & error, logos::stream& stream);

    void DeserializeDB(bool & erro, logos::stream& stream) override;

    uint64_t Serialize(logos::stream & stream) const override;

    boost::property_tree::ptree SerializeJson() const override;

    bool operator==(const Proxy & other) const;

    void Hash(blake2b_state& hash) const override;

    using Request::Hash;

    Amount lock_proxy;
    AccountAddress rep;
    uint32_t epoch_num;
    BlockHash staking_subchain_previous;   
};
