#pragma once

#include <logos/common.hpp>
#include <logos/lib/numbers.hpp>
#include <logos/request/request.hpp>


using AccountAddress = logos::uint256_union;

struct ElectionVote : Request
{
    struct CandidateVotePair
    {

        CandidateVotePair(const AccountAddress & account,
                uint8_t num_votes)
            : account(account)
            , num_votes(num_votes)
        {}

        CandidateVotePair(bool & error,
                logos::stream & stream)
        {
            Deserialize(error, stream);
        }

        uint64_t Serialize(logos::stream& stream) const
        {
            return logos::write(stream, account) +
                logos::write(stream, num_votes);
        }

        void Deserialize(bool & error, logos::stream & stream)
        {
            error = logos::read(stream, account);
            if(error)
            {
                return;
            }
            error = logos::read(stream, num_votes);
        }

        static uint64_t WireSize()
        {
            return sizeof(account) + sizeof(num_votes);
        }

        bool operator==(const CandidateVotePair& other) const
        {
            return account == other.account && num_votes == other.num_votes;
        }

        AccountAddress account;
        uint8_t num_votes;
    };

    ElectionVote() = default;

    ElectionVote(const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountPrivKey & priv,
            const AccountPubKey & pub);

    ElectionVote(const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountSig & signature);

    ElectionVote(bool & error,
            std::basic_streambuf<uint8_t> & stream);

    ElectionVote(bool & error,
            boost::property_tree::ptree const & tree);

    ElectionVote(bool & error,
                 const logos::mdb_val & mdbval);

    void Hash(blake2b_state& hash) const override;

    using Request::Hash;

    uint16_t WireSize() const override;

    boost::property_tree::ptree SerializeJson() const override;

    uint64_t Serialize(logos::stream & stream) const override;

    bool operator==(const ElectionVote & other) const;

    bool operator!=(const ElectionVote & other) const;

    //the accounts im voting for
    std::vector<CandidateVotePair> votes_; 
};

struct AnnounceCandidacy : Request
{
    AnnounceCandidacy(const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountPrivKey & priv,
            const AccountPubKey & pub);

    AnnounceCandidacy(const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountSig & signature);

    AnnounceCandidacy(bool & error,
            std::basic_streambuf<uint8_t> & stream);

    AnnounceCandidacy(bool & error,
            boost::property_tree::ptree const & tree);

};

struct RenounceCandidacy : Request
{
    RenounceCandidacy(const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountPrivKey & priv,
            const AccountPubKey & pub);

    RenounceCandidacy(const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountSig & signature);

    RenounceCandidacy(bool & error,
            std::basic_streambuf<uint8_t> & stream);

    RenounceCandidacy(bool & error,
            boost::property_tree::ptree const & tree);

};




