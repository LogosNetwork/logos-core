#pragma once

#include <logos/common.hpp>
#include <logos/lib/numbers.hpp>
#include <logos/request/request.hpp>


using AccountAddress = logos::uint256_union;


struct ElectionVote : Request
{

    using Votes = std::vector<std::pair<AccountAddress,uint8_t>>;

    ElectionVote();

    void Hash(blake2b_state& hash) const override;

    uint16_t WireSize() const override;

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;

    //the accounts im voting for
    Votes votes; 
};

struct AnnounceCandidacy : Request
{
    AnnounceCandidacy(const BlockHash & previous); 

    AnnounceCandidacy(bool & error,
            std::basic_streambuf<uint8_t> & stream);

    AnnounceCandidacy(bool & error,
            boost::property_tree::ptree const & tree);

};

struct RenounceCandidacy : Request
{
    RenounceCandidacy(const BlockHash & previous);  

    RenounceCandidacy(bool & error,
            std::basic_streambuf<uint8_t> & stream);

    RenounceCandidacy(bool & error,
            boost::property_tree::ptree const & tree);

};




