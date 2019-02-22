#pragma once

#include <logos/consensus/messages/byte_arrays.hpp>
#include <logos/request/transaction.hpp>
#include <logos/request/request.hpp>
#include <logos/node/utility.hpp>

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
           AccountPubKey const & pub,
           uint64_t work = 0);

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
           AccountSig const & sig,
           uint64_t work = 0);

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

    static const uint8_t MAX_TRANSACTIONS = 8;

    Transactions      transactions;
    uint64_t          work = 0;
};
