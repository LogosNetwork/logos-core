#pragma once

#include <logos/common.hpp>
#include <logos/lib/numbers.hpp>
#include <logos/request/request.hpp>
#include <boost/optional.hpp>


using AccountAddress = logos::uint256_union;

const size_t MAX_VOTES = 8;
const Amount MIN_REP_STAKE = 1;
const Amount MIN_DELEGATE_STAKE = 1;

//TODO: add hashing for all
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

    ElectionVote() : Request()
    {
        type = RequestType::ElectionVote; 
    }

    ElectionVote(const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence);

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

    void Deserialize(bool & error, logos::stream & stream);

    void DeserializeDB(bool & error, logos::stream & stream) override;


    bool operator==(const ElectionVote & other) const;

    bool operator!=(const ElectionVote & other) const;

    //the accounts im voting for
    std::vector<CandidateVotePair> votes; 
    uint32_t epoch_num;
};

struct AnnounceCandidacy : Request
{
    AnnounceCandidacy() : Request()
    {
        type = RequestType::AnnounceCandidacy;
    }

    AnnounceCandidacy(const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence);

    AnnounceCandidacy(const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountSig & signature);

    AnnounceCandidacy(bool & error,
            std::basic_streambuf<uint8_t> & stream);

    AnnounceCandidacy(bool & error,
            boost::property_tree::ptree const & tree);

    AnnounceCandidacy(bool& error,
            const logos::mdb_val& mdbval);


    void Deserialize(bool & error, logos::stream& stream);

    void DeserializeDB(bool & erro, logos::stream& stream) override;

    uint64_t Serialize(logos::stream & stream) const override;

    boost::property_tree::ptree SerializeJson() const override;

    bool operator==(const AnnounceCandidacy & other) const;

    uint16_t WireSize() const override;

    void Hash(blake2b_state& hash) const override;

    using Request::Hash;

    Amount stake;
    DelegatePubKey bls_key;
    uint32_t epoch_num;
    ByteArray<32> encryption_key;
};

struct RenounceCandidacy : Request
{
    RenounceCandidacy() : Request()
    {
        type = RequestType::RenounceCandidacy;
    }

    RenounceCandidacy(const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence);

    RenounceCandidacy(const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountSig & signature);

    RenounceCandidacy(bool & error,
            std::basic_streambuf<uint8_t> & stream);

    RenounceCandidacy(bool & error,
            boost::property_tree::ptree const & tree);

    RenounceCandidacy(bool& error,
            const logos::mdb_val& mdbval);

    void Deserialize(bool & error, logos::stream& stream);

    void DeserializeDB(bool & erro, logos::stream& stream) override;

    uint64_t Serialize(logos::stream & stream) const override;

    boost::property_tree::ptree SerializeJson() const override;

    bool operator==(const RenounceCandidacy & other) const;

    void Hash(blake2b_state& hash) const override;

    using Request::Hash;

    uint32_t epoch_num;
};

struct StartRepresenting : Request
{

    StartRepresenting() : Request()
    {
        type = RequestType::StartRepresenting;
    }

    StartRepresenting(const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const Amount stake);

    StartRepresenting(const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountSig & signature,
            const Amount stake);

    StartRepresenting(bool & error,
            std::basic_streambuf<uint8_t> & stream);

    StartRepresenting(bool & error,
            boost::property_tree::ptree const & tree);

    StartRepresenting(bool& error,
            const logos::mdb_val& mdbval);

    void Deserialize(bool & error, logos::stream & stream);

    void DeserializeDB(bool & error, logos::stream & stream) override;

    uint64_t Serialize(logos::stream & stream) const override;

    boost::property_tree::ptree SerializeJson() const override;

    bool operator==(const StartRepresenting & other) const;

    void Hash(blake2b_state& hash) const override;

    using Request::Hash;
    Amount stake;

    uint32_t epoch_num;
};

struct StopRepresenting : Request
{
    
    StopRepresenting() : Request()
    {
        type = RequestType::StopRepresenting;
    }

    StopRepresenting(const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence);

    StopRepresenting(const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountSig & signature);

    StopRepresenting(bool & error,
            std::basic_streambuf<uint8_t> & stream);

    StopRepresenting(bool & error,
            boost::property_tree::ptree const & tree);

    StopRepresenting(bool& error,
            const logos::mdb_val& mdbval);

    void Deserialize(bool& error, logos::stream& stream);

    void DeserializeDB(bool& error, logos::stream& stream) override;

    uint64_t Serialize(logos::stream & stream) const override;

    boost::property_tree::ptree SerializeJson() const override;

    bool operator==(const StopRepresenting & other) const;

    void Hash(blake2b_state& hash) const override;

    using Request::Hash;
    uint32_t epoch_num;
};




