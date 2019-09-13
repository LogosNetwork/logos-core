#pragma once

#include <logos/request/transaction.hpp>
#include <logos/node/utility.hpp>
#include <logos/lib/utility.hpp>
#include <logos/lib/numbers.hpp>
#include <logos/common.hpp>

#include <boost/property_tree/ptree.hpp>

enum class RequestType : uint8_t
{
    // Native Logos Requests
    //
    Send               = 0,
    Proxy              = 1,
    Issuance           = 2,

    // Administrative Token
    // Requests
    //
    IssueAdditional   = 3,
    ChangeSetting     = 4,
    ImmuteSetting     = 5,
    Revoke            = 6,
    AdjustUserStatus  = 7,
    AdjustFee         = 8,
    UpdateIssuerInfo  = 9,
    UpdateController  = 10,
    Burn              = 11,
    Distribute        = 12,
    WithdrawFee       = 13,
    WithdrawLogos     = 14,

    // Token User Requests
    //
    TokenSend         = 15,

    //Election Requests
    ElectionVote      = 16,
    AnnounceCandidacy = 17,
    RenounceCandidacy = 18,
    StartRepresenting = 19,
    StopRepresenting  = 20,
    Stake             = 21,
    Unstake           = 22,

    // Reward Requests
    //
    Claim             = 24,

    // Unknown
    //
    Unknown           = 23
};

class Reservations;

struct Request
{
    using BlockHash      = logos::block_hash;
    using AccountAddress = logos::uint256_union;

    struct Locator
    {
        Locator() = default;

        Locator(bool & error,
                logos::stream & stream);

        uint64_t Serialize(logos::stream & stream);

        BlockHash hash  = 0;
        uint16_t  index = 0;
    };

    Request() = default;

    Request(RequestType type);

    Request(RequestType type,
            const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence);

    Request(RequestType type,
            const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountSig & signature);

    Request(bool & error,
            const logos::mdb_val & mdbval);

    Request(bool & error,
            logos::stream & stream);

    Request(bool & error,
            boost::property_tree::ptree const & tree);

    virtual ~Request() = default;

    virtual logos::AccountType GetAccountType() const;
    virtual logos::AccountType GetSourceType() const;

    /// This method returns the account that will
    /// own the request. Eg. For TokenSend requests
    /// this will be the origin, but for Revoke
    /// requests, this will be the token account.
    /// @return The address of the owning account.
    virtual AccountAddress GetAccount() const;

    /// This method returns the account from which
    /// an amount is being deducted. For most accounts
    /// this will be the origin, but for Revoke
    /// commands, this is not the case.
    /// @return The address of the source account.
    virtual AccountAddress GetSource() const;

    virtual Amount GetLogosTotal() const;
    virtual Amount GetTokenTotal() const;

    void Sign(AccountPrivKey const & priv);
    void Sign(AccountPrivKey const & priv, AccountPubKey const & pub);
    void SignAndHash(bool & error, boost::property_tree::ptree const & ptree);
    bool VerifySignature(AccountPubKey const & pub) const;

    std::string ToJson() const;
    uint64_t ToStream(logos::stream & stream, bool with_work = false) const;
    logos::mdb_val ToDatabase(std::vector<uint8_t> & buf, bool with_work = false) const;
    virtual void DeserializeDB(bool & error, logos::stream & stream);

    virtual boost::property_tree::ptree SerializeJson() const;
    uint64_t DoSerialize(logos::stream & stream) const;
    virtual uint64_t Serialize(logos::stream & stream) const;
    void Deserialize(bool & error, logos::stream & stream);

    virtual bool Validate(logos::process_return & result,
                          std::shared_ptr<logos::Account> info) const;
    virtual bool Validate(logos::process_return & result) const;

    virtual BlockHash GetHash () const;
    BlockHash Hash() const;
    virtual void Hash(blake2b_state & hash) const;

    virtual uint16_t WireSize() const;

    virtual bool operator==(const Request & other) const;

    static const uint8_t MAX_TRANSACTIONS = 8;

    RequestType       type = RequestType::Unknown;
    AccountAddress    origin;
    BlockHash         previous;
    Amount            fee;
    uint32_t          sequence = 0;
    AccountSig        signature;
    uint64_t          work     = 0;
    BlockHash         next;
    mutable Locator   locator;
    mutable BlockHash digest;
};

struct Send : Request
{
    using Request::Hash;

    using Transaction  = ::Transaction<Amount>;
    using Transactions = std::vector<Transaction>;

    Send ();

    /// Class constructor
    /// Note that if additional transaction is added after construction, the StateBlock must be re-signed
    /// @param account the account creating the StateBlock
    /// @param previous the hash of the previous StateBlock on the account's send chain
    /// @param sequence the sequence number
    /// @param to the target account, additional targets can be added with AddTransaction()
    /// @param amount the transaction amount, additional amounts can be added with AddTransaction()
    /// @param transaction_fee the transaction fee
    /// @param priv the private EdDSA key of the account
    /// @param pub the public EdDSA key of the account
    /// @param work the prove of work
    Send ( AccountAddress const & account,
           BlockHash const & previous,
           uint32_t sequence,
           AccountAddress const & to,
           Amount const & amount,
           Amount const & transaction_fee,
           AccountPrivKey const & priv,
           AccountPubKey const & pub);

    /// Class constructor
    /// Note that if additional transaction is added after construction, the StateBlock must be re-signed
    /// @param account the account creating the StateBlock
    /// @param previous the hash of the previous StateBlock on the account's send chain
    /// @param sequence the sequence number
    /// @param to the target account, additional targets can be added with AddTransaction()
    /// @param amount the transaction amount, additional amounts can be added with AddTransaction()
    /// @param transaction_fee the transaction fee
    /// @param sig the EdDSA signature of the StateBlock
    /// @param work the prove of work
    Send ( AccountAddress const & account,
           BlockHash const & previous,
           uint32_t sequence,
           AccountAddress const & to,
           Amount const & amount,
           Amount const & transaction_fee,
           AccountSig const & sig);

    /// Class constructor
    /// construct from deserializing a property_tree which was decoded from a Json string
    /// @param error it will be set to true if deserialization fail [out]
    /// @param tree the property_tree to deserialize from
    /// @param with_batch_hash if the property_tree should contain the batch_hash
    /// @param with_work if the property_tree should contain the prove of work
    Send(bool & error,
         const boost::property_tree::ptree & tree);

    /// Constructor from deserializing a buffer read from the database
    /// @param error it will be set to true if deserialization fail [out]
    /// @param mdbval the buffer read from the database
    Send(bool & error,
         const logos::mdb_val & mdbval);

    /// Class constructor
    /// construct from deserializing a stream which was decoded from a Json string
    /// @param error it will be set to true if deserialization fail [out]
    /// @param stream the stream containing serialized data [in]
    /// @param with_batch_hash if the serialized data should have the batch_hash [in]
    Send (bool & error,
          logos::stream & stream);

    Amount GetLogosTotal() const override;

    /// Add a new transaction
    /// @param to the target account
    /// @param amount the transaction amount
    /// @returns if the new transaction is added.
    bool AddTransaction(const AccountAddress & to, const Amount & amount);
    bool AddTransaction(const Transaction & transaction);

    /// Add the data members to a hash context
    /// @param hash the hash context
    void Hash(blake2b_state & hash) const override;

    /// Get the send request hash without re-computing it
    /// @returns the hash value
    BlockHash GetHash () const override;

    /// Serialize the data members to a property_tree which will be encoded to Json
    /// @param tree the property_tree to add data members to
    /// @param with_batch_hash if batch_hash should be serialized
    /// @param with_work if prove of work should be serialized
    boost::property_tree::ptree SerializeJson() const override;

    /// Serialize the data members to a stream
    /// @param stream the stream to serialize to
    /// @returns the number of bytes serialized
    uint64_t Serialize (logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool &error, logos::stream &stream) override;

    bool operator==(const Request & other) const override;

    Transactions      transactions;
};