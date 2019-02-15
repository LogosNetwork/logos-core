#pragma once

#include <logos/lib/numbers.hpp>
#include <logos/node/utility.hpp>

struct RepInfo
{
    RepInfo ();
    RepInfo (bool & error, const logos::mdb_val & mdbval);
    RepInfo (const RepInfo &) = default;

    RepInfo (logos::block_hash const & candidacy_action_tip,
                  logos::block_hash const & election_vote_tip,
                  logos::block_hash const & rep_action_tip);

    uint32_t serialize (logos::stream &) const;
    bool deserialize (logos::stream &);
    bool operator== (RepInfo const &) const;
    bool operator!= (RepInfo const &) const;
    //logos::mdb_val val () const;
    logos::mdb_val to_mdb_val(std::vector<uint8_t> &buf) const;

    //are these tips really needed? we store most info in the flags
    //which is more efficient because we do less database reads
    //i think we still need election_vote_tip, so that way
    //we can easily find out who voted for who, but the rep_action_tip
    //and candidacy_action_tip seem uneccessary
    logos::block_hash candidacy_action_tip;
    logos::block_hash election_vote_tip;
    uint32_t last_epoch_voted;
    logos::block_hash rep_action_tip;
    bool announced_stop;
    uint32_t rep_action_epoch;

    uint32_t stake = 1; //TODO what type should this be?
};

struct CandidateInfo
{
    CandidateInfo () = default;
    CandidateInfo (bool & error, const logos::mdb_val & mdbval);
    CandidateInfo (const CandidateInfo &) = default;
    CandidateInfo (bool active, bool remove, uint64_t votes);
    uint32_t serialize(logos::stream &) const;
    bool deserialize(logos::stream &);
    bool operator==(const CandidateInfo &) const;
    bool operator!=(const CandidateInfo &) const;
    logos::mdb_val to_mdb_val(std::vector<uint8_t> & buf) const;

    bool active = false;
    bool remove = false;
    uint64_t votes_received_weighted = 0;
};

