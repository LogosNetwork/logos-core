#pragma once

#include <logos/request/requests.hpp>

using AccountAddress = logos::uint256_union;

const size_t MAX_VOTES = 8;

// TODO: With inflation, total supply will increase over time.
//       These need to be dynamic.
//
const Amount MIN_REP_STAKE = std::numeric_limits<logos::uint128_t>::max () / 10000;
const Amount MIN_DELEGATE_STAKE = std::numeric_limits<logos::uint128_t>::max () / 1000;

struct Governance : Request
{
    Governance() = default;

    Governance(RequestType type);

    Governance(bool & error,
               std::basic_streambuf<uint8_t> & stream);

    Governance(bool & error,
               boost::property_tree::ptree const & tree);

    Governance(bool& error,
               const logos::mdb_val& mdbval);

    void Deserialize(bool & error, logos::stream& stream);

    void DeserializeDB(bool & error, logos::stream& stream) override;

    uint64_t Serialize(logos::stream & stream) const override;

    boost::property_tree::ptree SerializeJson() const override;

    bool operator==(const Governance & other) const;

    void Hash(blake2b_state& hash) const override;

    using Request::Hash;

    uint32_t  epoch_num;
    BlockHash governance_subchain_prev;
};

struct Proxy : Governance
{
    Proxy();

    Proxy(bool & error,
          std::basic_streambuf<uint8_t> & stream);

    Proxy(bool & error,
          boost::property_tree::ptree const & tree);

    Proxy(bool& error,
          const logos::mdb_val& mdbval);

    void Deserialize(bool & error, logos::stream& stream);

    void DeserializeDB(bool & error, logos::stream& stream) override;

    uint64_t Serialize(logos::stream & stream) const override;

    boost::property_tree::ptree SerializeJson() const override;

    bool operator==(const Proxy & other) const;

    void Hash(blake2b_state& hash) const override;

    using Request::Hash;

    Amount         lock_proxy;
    AccountAddress rep;
};

struct Stake : Governance
{
    Stake();

    Stake(bool & error,
          std::basic_streambuf<uint8_t> & stream);

    Stake(bool & error,
          boost::property_tree::ptree const & tree);

    Stake(bool& error,
          const logos::mdb_val& mdbval);

    void Deserialize(bool & error, logos::stream& stream);

    void DeserializeDB(bool & erro, logos::stream& stream) override;

    uint64_t Serialize(logos::stream & stream) const override;

    boost::property_tree::ptree SerializeJson() const override;

    bool operator==(const Stake & other) const;

    void Hash(blake2b_state& hash) const override;

    using Request::Hash;

    Amount stake;
};

struct Unstake : Governance
{
    Unstake();

    Unstake(bool & error,
            std::basic_streambuf<uint8_t> & stream);

    Unstake(bool & error,
            boost::property_tree::ptree const & tree);

    Unstake(bool& error,
            const logos::mdb_val& mdbval);

    void Deserialize(bool & error, logos::stream& stream);

    void DeserializeDB(bool & erro, logos::stream& stream) override;

    uint64_t Serialize(logos::stream & stream) const override;

    bool operator==(const Unstake & other) const;
};

struct ElectionVote : Governance
{
    struct CandidateVotePair
    {

        CandidateVotePair(const std::string & account,
                          uint8_t num_votes);

        CandidateVotePair(const AccountAddress & account,
                          uint8_t num_votes);

        CandidateVotePair(bool & error,
                          boost::property_tree::ptree const & tree);

        CandidateVotePair(bool & error,
                          logos::stream & stream);

        void DeserializeJson(bool & error,
                             boost::property_tree::ptree const & tree);

        boost::property_tree::ptree SerializeJson() const;

        uint64_t Serialize(logos::stream& stream) const;

        void Deserialize(bool & error, logos::stream & stream);

        static uint64_t WireSize();

        bool operator==(const CandidateVotePair& other) const;

        AccountAddress account;
        uint8_t        num_votes;
    };

    ElectionVote();

    ElectionVote(bool & error,
                 std::basic_streambuf<uint8_t> & stream);

    ElectionVote(bool & error,
                 boost::property_tree::ptree const & tree);

    ElectionVote(bool & error,
                 const logos::mdb_val & mdbval);

    void Hash(blake2b_state& hash) const override;

    using Request::Hash;

    boost::property_tree::ptree SerializeJson() const override;

    uint64_t Serialize(logos::stream & stream) const override;

    void Deserialize(bool & error, logos::stream & stream);

    void DeserializeDB(bool & error, logos::stream & stream) override;

    bool operator==(const ElectionVote & other) const;

    bool operator!=(const ElectionVote & other) const;

    // The accounts im voting for
    std::vector<CandidateVotePair> votes;
};

struct AnnounceCandidacy : Governance
{
    AnnounceCandidacy();

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

    void Hash(blake2b_state& hash) const override;

    using Request::Hash;

    // If set_stake is true, this request will adjust origin's self stake
    // to the amount specified in the stake field
    // If set_stake is false, this request will ignore the stake field,
    // and origin's self stake will remain the same as before this request
    //
    bool           set_stake;
    Amount         stake;
    DelegatePubKey bls_key;
    ECIESPublicKey ecies_key;
    uint8_t        levy_percentage;
};

struct RenounceCandidacy : Governance
{
    RenounceCandidacy();

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

    // If set_stake is true, this request will adjust origin's self stake
    // to the amount specified in the stake field
    // If set_stake is false, this request will ignore the stake field,
    // and origin's self stake will remain the same as before this request
    //
    bool   set_stake;
    Amount stake;
};

struct StartRepresenting : Governance
{
    StartRepresenting();

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

    // If set_stake is true, this request will adjust origin's self stake
    // to the amount specified in the stake field
    // If set_stake is false, this request will ignore the stake field,
    // and origin's self stake will remain the same as before this request
    //
    bool    set_stake;
    Amount  stake;
    uint8_t levy_percentage;
};

struct StopRepresenting : Governance
{
    StopRepresenting();

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

    // If set_stake is true, this request will adjust origin's self stake
    // to the amount specified in the stake field
    // If set_stake is false, this request will ignore the stake field,
    // and origin's self stake will remain the same as before this request
    //
    bool   set_stake;
    Amount stake;
};
