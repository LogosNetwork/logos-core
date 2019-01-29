#pragma once

#include <logos/consensus/messages/byte_arrays.hpp>
#include <logos/request/request.hpp>
#include <logos/node/utility.hpp>

#include <ed25519-donna/ed25519.h>

struct Send : Request
{
    // Note: If MAX_TRANSACTIONS is increased we may also need
    //       to increase the network layer buffer size.
    static const uint8_t MAX_TRANSACTIONS = 8;

    /// A transaction in a StateBlock
    struct Transaction
    {
        Transaction(AccountAddress const & to,
                    Amount const & amount);

        Transaction(bool & error,
                    logos::stream & stream);

        Transaction() = default;

        uint64_t Serialize(logos::stream & stream) const;

        void Deserialize(bool & error, logos::stream & stream);

        AccountAddress target;
        Amount         amount;
    };

    using Transactions = std::vector<Transaction>;

    Send () = default;

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
         boost::property_tree::ptree const & tree,
         bool with_batch_hash = false,
         bool with_work = false);

    /// Constructor from deserializing a buffer read from the database
    /// @param error it will be set to true if deserialization fail [out]
    /// @param mdbval the buffer read from the database
    Send(bool & error, const logos::mdb_val & mdbval);

    /// Class constructor
    /// construct from deserializing a stream which was decoded from a Json string
    /// @param error it will be set to true if deserialization fail [out]
    /// @param stream the stream containing serialized data [in]
    /// @param with_batch_hash if the serialized data should have the batch_hash [in]
    Send (bool & error,
          logos::stream & stream,
          bool with_batch_hash = false);

    /// Add a new transaction
    /// @param to the target account
    /// @param amount the transaction amount
    /// @returns if the new transaction is added.
    bool AddTransaction(AccountAddress const & to, Amount const & amount);

    BlockHash Hash() const override;

    /// Add the data members to a hash context
    /// @param hash the hash context
    void Hash(blake2b_state & hash) const override;

    /// Signs the StateBlock
    /// @param priv the private EdDSA key of the account
    /// @param pub the public EdDSA key of the account
    void Sign(AccountPrivKey const & priv, AccountPubKey const & pub);

    /// Verify the signature of the StateBlock
    /// @param pub the public EdDSA key of the account
    /// @return true if the signature is valid
    bool VerifySignature(AccountPubKey const & pub) const;

    /// Get the send request hash without re-computing it
    /// @returns the hash value
    BlockHash GetHash () const override;

    /// Get the number of transactions in the Send
    /// @return the number of transactions in the Send
    uint8_t GetNumTransactions() const;

    /// Serialize the data members to a property_tree which will be encoded to Json
    /// @param tree the property_tree to add data members to
    /// @param with_batch_hash if batch_hash should be serialized
    /// @param with_work if prove of work should be serialized
    boost::property_tree::ptree SerializeJson() const override;

    /// Serialize the data members to a stream
    /// @param stream the stream to serialize to
    /// @param with_batch_hash if batch_hash should be serialized
    /// @returns the number of bytes serialized
    uint64_t Serialize (logos::stream & stream) const override;

    AccountAddress    account;
    BlockHash         previous;
    uint32_t          sequence = 0;
    Transactions      transactions;
    Amount            transaction_fee;
    AccountSig        signature;
    uint64_t          work = 0;
    mutable BlockHash digest;
    mutable BlockHash batch_hash = 0;
    mutable uint16_t  index_in_batch = 0;
};
