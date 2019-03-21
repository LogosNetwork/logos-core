#pragma once

#include <logos/lib/numbers.hpp>
#include <logos/node/utility.hpp>
#include <logos/elections/requests.hpp>

struct RepInfo
{
    RepInfo ();
    RepInfo (bool & error, const logos::mdb_val & mdbval);
    RepInfo (const RepInfo &) = default;

    RepInfo (logos::block_hash const & candidacy_action_tip,
                  logos::block_hash const & election_vote_tip,
                  logos::block_hash const & rep_action_tip);

    RepInfo(const StartRepresenting& request);

    RepInfo(const AnnounceCandidacy& request);

    uint32_t serialize (logos::stream &) const;
    bool deserialize (logos::stream &);
    bool operator== (RepInfo const &) const;
    bool operator!= (RepInfo const &) const;
    logos::mdb_val to_mdb_val(std::vector<uint8_t> &buf) const;

    boost::property_tree::ptree SerializeJson() const;
    
    //are these tips really needed? we store most info in the flags
    //which is more efficient because we do less database reads
    //i think we still need election_vote_tip, so that way
    //we can easily find out who voted for who, but the rep_action_tip
    //and candidacy_action_tip seem uneccessary
    logos::block_hash candidacy_action_tip;
    logos::block_hash election_vote_tip;
    logos::block_hash rep_action_tip;
    Amount stake; 
};
