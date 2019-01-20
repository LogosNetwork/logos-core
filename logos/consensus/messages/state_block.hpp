///
/// @file
/// This file contains declaration and implementation of ReceiveBlock and StateBlock
///
#pragma once

#include <vector>

#include <logos/consensus/messages/common.hpp>
#include <logos/lib/log.hpp>
#include <logos/node/utility.hpp>
#include <blake2/blake2.h>
#include <ed25519-donna/ed25519.h>

/// An item on the receive chain of an account. A ReceiveBlock is created for each transaction is a StateBlock
struct ReceiveBlock
{
    BlockHash previous;
    BlockHash send_hash;
    uint16_t index2send = 0;

    ReceiveBlock() = default;

    /// Class constructor
    /// @param previous the hash of the previous ReceiveBlock on the account chain
    /// @param send_hash the hash of the StateBlock
    /// @param index2send the index to the array of transactions in the StateBlock
    ReceiveBlock(const BlockHash & previous, const BlockHash & send_hash, uint16_t index2send = 0)
    : previous(previous), send_hash(send_hash), index2send(index2send)
    {}

    /// Constructor from deserializing a buffer read from the database
    /// @param error it will be set to true if deserialization fail [out]
    /// @param mdbval the buffer read from the database
    ReceiveBlock(bool & error, const logos::mdb_val & mdbval)
    {
        logos::bufferstream stream(reinterpret_cast<uint8_t const *> (mdbval.data()), mdbval.size());

        error = logos::read(stream, previous);
        if(error)
        {
            return;
        }

        error = logos::read(stream, send_hash);
        if(error)
        {
            return;
        }

        error = logos::read(stream, index2send);
        if(error)
        {
            return;
        }
        index2send= le16toh(index2send);
    }

    /// Serialize the data members to a Json string
    /// @returns the Json string
    std::string SerializeJson() const
    {
        boost::property_tree::ptree tree;
        SerializeJson (tree);
        std::stringstream ostream;
        boost::property_tree::write_json(ostream, tree);
        return ostream.str();
    }

    /// Add the data members to the property_tree which will be encoded to Json
    /// @param batch_state_block the property_tree to add data members to
    void SerializeJson(boost::property_tree::ptree & tree) const
    {
        tree.put("previous", previous.to_string());
        tree.put("send_hash", send_hash.to_string());
        tree.put("index_to_send_block", std::to_string(index2send));
    }

    /// Serialize the data members to a stream
    /// @param stream the stream to serialize to
    void Serialize (logos::stream & stream) const
    {
        uint16_t idx = htole16(index2send);

        logos::write(stream, previous);
        logos::write(stream, send_hash);
        logos::write(stream, idx);
    }

    /// Compute the hash of the ReceiveBlock
    /// @returns the hash value computed
    BlockHash Hash() const
    {
        return Blake2bHash<ReceiveBlock>(*this);
    }

    /// Add the data members to a hash context
    /// @param hash the hash context
    void Hash(blake2b_state & hash) const
    {
        uint16_t s = htole16(index2send);
        // SYL integration fix: receive block shouldn't be hashed with previous, since this field might change
        send_hash.Hash(hash);
        blake2b_update(&hash, &s, sizeof(uint16_t));
    }

    /// Serialize the data members to a database buffer
    /// @param buf the memory buffer to serialize to
    /// @return the database buffer
    logos::mdb_val to_mdb_val(std::vector<uint8_t> &buf) const
    {
        assert(buf.empty());
        {
            logos::vectorstream stream(buf);
            Serialize(stream);
        }
        return logos::mdb_val(buf.size(), buf.data());
    }
};

struct StateBlock
{
    static const uint16_t MAX_TRANSACTION   = 8; //at most 2^16-1
    //Note: if increase MAX_TRANSACTION, may also need to increase network layer buffer size

    enum class Type : uint8_t
    {
        send,
        change,
        unknown = 0xff
    };

    static std::string TypeToStr(Type t)
    {
        switch (t) {
            case Type::send:
                return std::string("send");
            case Type::change:
                return std::string("change");
            default:
                return std::string("unknown");
        }
    }

    static Type StrToType(std::string &s)
    {
        if(s == std::string("send"))
            return Type::send;
        else if (s == std::string("change"))
            return Type::change;
        else
            return Type::unknown;
    }

    /// A transaction in a StateBlock
    struct Transaction{
        AccountAddress  target;
        Amount          amount;
        Transaction(AccountAddress const & to, Amount const & amount)
        : target(to)
        , amount(amount)
        {}
        Transaction()=default;
    };

    using Transactions = std::vector<Transaction>;

    StateBlock () = default;

    /// Class constructor
    /// Note that if additional transaction is added after construction, the StateBlock must be re-signed
    /// @param account the account creating the StateBlock
    /// @param previous the hash of the previous StateBlock on the account's send chain
    /// @param sequence the sequence number
    /// @param type the type of the StateBlock
    /// @param to the target account, additional targets can be added with AddTransaction()
    /// @param amount the transaction amount, additional amounts can be added with AddTransaction()
    /// @param transaction_fee the transaction fee
    /// @param priv the private EdDSA key of the account
    /// @param pub the public EdDSA key of the account
    /// @param work the prove of work
    StateBlock ( AccountAddress const & account,
                 BlockHash const & previous,
                 uint32_t sequence,
                 Type type,
                 AccountAddress const & to,
                 Amount const & amount,
                 Amount const & transaction_fee,
                 AccountPrivKey const & priv,
                 AccountPubKey const & pub,
                 uint64_t work = 0)
    : account(account)
    , previous(previous)
    , sequence(sequence)
    , type(type)
    , trans()
    , transaction_fee(transaction_fee)
    , signature()
    , work(work)
    {
        trans.push_back(Transaction(to, amount));
        Sign(priv, pub);
    }

    /// Class constructor
    /// Note that if additional transaction is added after construction, the StateBlock must be re-signed
    /// @param account the account creating the StateBlock
    /// @param previous the hash of the previous StateBlock on the account's send chain
    /// @param sequence the sequence number
    /// @param type the type of the StateBlock
    /// @param to the target account, additional targets can be added with AddTransaction()
    /// @param amount the transaction amount, additional amounts can be added with AddTransaction()
    /// @param transaction_fee the transaction fee
    /// @param sig the EdDSA signature of the StateBlock
    /// @param work the prove of work
    StateBlock ( AccountAddress const & account,
                 BlockHash const & previous,
                 uint32_t sequence,
                 Type type,
                 AccountAddress const & to,
                 Amount const & amount,
                 Amount const & transaction_fee,
                 AccountSig const & sig,
                 uint64_t work = 0)
    : account(account)
    , previous(previous)
    , sequence(sequence)
    , type(type)
    , trans()
    , transaction_fee(transaction_fee)
    , signature(sig)
    , work(work)
    {
        trans.push_back(Transaction(to, amount));
        Hash();
    }

    /// Class constructor
    /// construct from deserializing a property_tree which was decoded from a Json string
    /// @param error it will be set to true if deserialization fail [out]
    /// @param tree the property_tree to deserialize from
    /// @param with_batch_hash if the property_tree should contain the batch_hash
    /// @param with_work if the property_tree should contain the prove of work
    StateBlock(bool & error, boost::property_tree::ptree const & tree,
            bool with_batch_hash = false, bool with_work = false);

    /// Class constructor
    /// construct from deserializing a stream which was decoded from a Json string
    /// @param error it will be set to true if deserialization fail [out]
    /// @param stream the stream containing serialized data [in]
    /// @param with_batch_hash if the serialized data should have the batch_hash [in]
    StateBlock (bool & error, logos::stream & stream, bool with_batch_hash = false)
    {
        error = logos::read(stream, account);
        if(error)
        {
            return;
        }

        error = logos::read(stream, previous);
        if(error)
        {
            return;
        }

        error = logos::read(stream, sequence);
        if(error)
        {
            return;
        }
        sequence = le32toh(sequence);

        error = logos::read(stream, type);
        if(error)
        {
            return;
        }

        uint16_t countle;
        error = logos::read(stream, countle);
        if(error)
        {
            return;
        }
        uint16_t count = le16toh(countle);

        for(size_t i = 0; i < count; ++i)
        {
            AccountAddress  target;
            Amount          amount;
            error = logos::read(stream, target);
            if(error)
            {
                return;
            }

            error = logos::read(stream, amount);
            if(error)
            {
                return;
            }
            trans.push_back(Transaction(target, amount));
        }

        error = logos::read(stream, transaction_fee);
        if(error)
        {
            return;
        }

        error = logos::read(stream, signature);
        if(error)
        {
            return;
        }

        if(with_batch_hash)
        {
            error = logos::read(stream, batch_hash);
            if(error)
            {
                return;
            }
            uint16_t idx;
            error = logos::read(stream, idx);
            if(error)
            {
                return;
            }
            index_in_batch = le16toh(idx);
        }

        Hash ();
    }

    /// Add a new transaction
    /// @param to the target account
    /// @param amount the transaction amount
    /// @returns if the new transaction is added.
    bool AddTransaction(AccountAddress const & to, Amount const & amount)
    {
        if(trans.size() < MAX_TRANSACTION)
        {
            trans.push_back(Transaction(to, amount));
            return true;
        }
        return false;
    }

    /// Compute the hash of the StateBlock
    /// @returns the hash value computed
    BlockHash Hash() const
    {
        digest = Blake2bHash<StateBlock>(*this);
        return digest;
    }

    /// Add the data members to a hash context
    /// @param hash the hash context
    void Hash(blake2b_state & hash) const
    {
        account.Hash(hash);
        previous.Hash(hash);
        uint32_t s = htole32(sequence);
        blake2b_update(&hash, &s, sizeof(uint32_t));
        blake2b_update(&hash, &type, sizeof(uint8_t));
        blake2b_update(&hash, transaction_fee.data(), ACCOUNT_AMOUNT_SIZE);
        uint16_t tran_count = trans.size();
        tran_count = htole16(tran_count);
        blake2b_update(&hash, &tran_count, sizeof(uint16_t));

        for (const auto & t : trans)
        {
            t.target.Hash(hash);
            blake2b_update(&hash, t.amount.data(), ACCOUNT_AMOUNT_SIZE);
        }
    }

    /**
     * Signs the StateBlock
     * @param priv the private EdDSA key of the account
     * @param pub the public EdDSA key of the account
     */
    void Sign(AccountPrivKey const & priv, AccountPubKey const & pub)
    {
        Hash();
        ed25519_sign (const_cast<BlockHash &>(digest).data(), HASH_SIZE, const_cast<AccountPrivKey&>(priv).data (), const_cast<AccountPubKey&>(pub).data (), signature.data ());
    }

    /**
     * Verify the signature of the StateBlock
     * @param pub the public EdDSA key of the account
     * @return true if the signature is valid
     */
    bool VerifySignature(AccountPubKey const & pub) const
    {
        return 0 == ed25519_sign_open (const_cast<BlockHash &>(digest).data(), HASH_SIZE, const_cast<AccountPubKey&>(pub).data (), const_cast<AccountSig&>(signature).data ());
    }

    /**Get the hash of the StateBlock without re-compute
     * @returns the hash value
     */
    BlockHash GetHash () const
    {
        return digest;
    }

    /*
     * Get the number of transactions in the StateBlock
     * @return the number of transactions in the StateBlock
     */
    uint16_t GetNumTransactions() const
    {
        return trans.size();
    }

    /** Serialize the data members to a Json string
     * @param with_batch_hash if batch_hash should be serialized
     * @param with_work if prove of work should be serialized
     * @returns the Json string
     */
    std::string SerializeJson(bool with_batch_hash = false, bool with_work = false) const
    {
        boost::property_tree::ptree tree;
        SerializeJson (tree, with_batch_hash, with_work);
        std::stringstream ostream;
        boost::property_tree::write_json(ostream, tree);
        return ostream.str();
    }

    /** Serialize the data members to a property_tree which will be encoded to Json
     * @param tree the property_tree to add data members to
     * @param with_batch_hash if batch_hash should be serialized
     * @param with_work if prove of work should be serialized
     */
    void SerializeJson(boost::property_tree::ptree & tree, bool with_batch_hash = false, bool with_work = false) const;

    /** Serialize the data members to a stream
     * @param stream the stream to serialize to
     * @param with_batch_hash if batch_hash should be serialized
     * @returns the number of bytes serialized
     */
    uint32_t Serialize (logos::stream & stream, bool with_batch_hash = false) const
    {
        uint32_t sqn = htole32(sequence);
        uint16_t count = trans.size();
        uint16_t count_le = htole16(count);

        uint32_t s = logos::write(stream, account);
        s += logos::write(stream, previous);
        s += logos::write(stream, sqn);
        s += logos::write(stream, type);
        s += logos::write(stream, count_le);
        for(const auto &t : trans)
        {
            s += logos::write(stream, t.target);
            s += logos::write(stream, t.amount);
        }
        s += logos::write(stream, transaction_fee);
        s += logos::write(stream, signature);
        if(with_batch_hash)
        {
            s += logos::write(stream, batch_hash);
            uint16_t idx_le = htole16(index_in_batch);
            s += logos::write(stream, idx_le);
        }

        return s;
    }

    /// Serialize the data members to a database buffer
    /// @param buf the memory buffer to serialize to
    /// @return the database buffer
    logos::mdb_val to_mdb_val(std::vector<uint8_t> & buf) const
    {
        assert(buf.empty());
        {
            logos::vectorstream stream(buf);
            Serialize(stream, true);
        }
        return logos::mdb_val(buf.size(), buf.data());
    }

    /// Constructor from deserializing a buffer read from the database
    /// @param error it will be set to true if deserialization fail [out]
    /// @param mdbval the buffer read from the database
    StateBlock(bool & error, const logos::mdb_val & mdbval)
    {
        logos::bufferstream stream(reinterpret_cast<uint8_t const *> (mdbval.data()), mdbval.size());
        new (this) StateBlock(error, stream, true);
    }

    AccountAddress account;
    BlockHash previous;
    uint32_t sequence;
    Type type;
    Transactions trans;
    Amount transaction_fee;
    AccountSig signature;

    uint64_t work = 0;
    mutable BlockHash digest;
    mutable BlockHash batch_hash = 0;
    mutable uint16_t index_in_batch = 0;
};

