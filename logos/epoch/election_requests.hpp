#pragma once

#include <logos/common.hpp>
#include <logos/lib/numbers.hpp>


using AccountAddress = logos::uint256_union;


struct ElectionVote : Request
{
    Vote();

    virtual ~Vote();

    void Hash(blake2b_state& hash) const override;

    uint16_t WireSize() const override;

    //the accounts im voting for
    std::vector<std::pair<AccountAddress,uint8_t>> votes; 
};

struct AnnounceCandidacy : Request
{
    Candidacy();  

    virtual ~Candidacy();

    void Hash(blake2b_state& hash) const override;

    uint16_t WireSize() const override;
};

struct RenounceCandidacy : Request
{
    Candidacy();  

    virtual ~Candidacy();

    void Hash(blake2b_state& hash) const override;

    uint16_t WireSize() const override;
};




