#pragma once

#include <logos/lib/numbers.hpp>
#include <logos/node/utility.hpp>
#include <logos/governance/requests.hpp>

struct CandidateInfo
{
    CandidateInfo ();
    CandidateInfo(const AnnounceCandidacy& request);
    CandidateInfo (bool & error, const logos::mdb_val & mdbval);
    CandidateInfo (const CandidateInfo &) = default;
    CandidateInfo (uint64_t votes);

    uint32_t serialize(logos::stream &) const;
    bool deserialize(logos::stream &);
    bool operator==(const CandidateInfo &) const;
    bool operator!=(const CandidateInfo &) const;
    logos::mdb_val to_mdb_val(std::vector<uint8_t> & buf) const;

    void TransitionIfNecessary(uint32_t cur_epoch);


    boost::property_tree::ptree SerializeJson() const;

    Amount votes_received_weighted;
    DelegatePubKey bls_key;
    ECIESPublicKey ecies_key;
    Amount cur_stake;
    Amount next_stake;
    uint32_t epoch_modified;
    uint8_t levy_percentage;
};
