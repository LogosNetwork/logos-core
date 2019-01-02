#pragma once

#include <vector>

#include <logos/consensus/messages/common.hpp>
#include <logos/lib/log.hpp>
#include <logos/node/utility.hpp>
#include <blake2/blake2.h>
#include <ed25519-donna/ed25519.h>

struct ReceiveBlock
{
    BlockHash previous;
    BlockHash send_hash;
    uint16_t index2send = 0;

    ReceiveBlock() = default;

    ReceiveBlock(const BlockHash & previous, const BlockHash & send_hash, uint16_t index2send = 0)
    : previous(previous), send_hash(send_hash), index2send(index2send)
    {}

    ReceiveBlock(bool & error, const logos::mdb_val & mdbval)
    {
        if(error)
        {
            return;
        }

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

    std::string SerializeJson() const
    {
        boost::property_tree::ptree tree;
        SerializeJson (tree);
        std::stringstream ostream;
        boost::property_tree::write_json(ostream, tree);
        return ostream.str();
    }

    void SerializeJson(boost::property_tree::ptree & tree) const
    {
        tree.put("previous", previous.to_string());
        tree.put("send_hash", send_hash.to_string());
        tree.put("index_to_send_block", std::to_string(index2send));
    }

    void Serialize (logos::stream & stream) const
    {
        uint16_t idx = htole16(index2send);

        logos::write(stream, previous);
        logos::write(stream, send_hash);
        logos::write(stream, idx);
    }

    BlockHash Hash() const
    {
        return Blake2bHash<ReceiveBlock>(*this);
    }

    void Hash(blake2b_state & hash) const
    {
        uint16_t s = htole16(index2send);
        previous.Hash(hash);
        send_hash.Hash(hash);
        blake2b_update(&hash, &s, sizeof(uint16_t));
    }


    logos::mdb_val to_mdb_val(std::vector<uint8_t> &buf) const
    {
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
        change
    };

    static std::string Type2Str(Type t)
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

    //    StateBlock (AccountAddress const & from, BlockHash const & previous, uint32_t sqn);
    //    StateBlock (bool &, boost::property_tree::ptree const &);

    StateBlock(const StateBlock & other)
    : account(other.account)
    , previous(other.previous)
    , sequence(other.sequence)
    , type(other.type)
    , trans(other.trans)
    , transaction_fee(other.transaction_fee)
    , signature(other.signature)
    , work(other.work)
    , degest(other.degest)
    {}

    StateBlock (bool & error, logos::stream & stream, bool with_batch_hash = false)
    {
        if(error)
        {
            return;
        }

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
        }

        Hash ();
    }

    bool AddTransaction(AccountAddress const & to, Amount const & amount)
    {
        if(trans.size() < MAX_TRANSACTION)
        {
            trans.push_back(Transaction(to, amount));
            return true;
        }
        return false;
    }


    BlockHash Hash() const
    {
        degest = Blake2bHash<StateBlock>(*this);
        return degest;
    }

    void Hash(blake2b_state & hash) const
    {
        account.Hash(hash);
        previous.Hash(hash);
        uint32_t s = htole32(sequence);
        blake2b_update(&hash, &s, sizeof(uint32_t));
        blake2b_update(&hash, &type, sizeof(uint8_t));
        blake2b_update(&hash, transaction_fee.bytes.data(), transaction_fee.bytes.size());
        signature.Hash(hash);
        uint16_t tran_count = trans.size();
        tran_count = htole16(tran_count);
        blake2b_update(&hash, &tran_count, sizeof(uint16_t));

        for (const auto & t : trans)
        {
            t.target.Hash(hash);
            blake2b_update(&hash, t.amount.bytes.data(), t.amount.bytes.size());
        }
    }


    void Sign(AccountPrivKey const & priv, AccountPubKey const & pub)
    {
        Hash();
        ed25519_sign (const_cast<BlockHash &>(degest).data(), HASH_SIZE, const_cast<AccountPrivKey&>(priv).data (), const_cast<AccountPubKey&>(pub).data (), signature.data ());
    }

    bool VerifySignature(AccountPubKey const & pub) const
    {
        return 0 == ed25519_sign_open (const_cast<BlockHash &>(degest).data(), HASH_SIZE, const_cast<AccountPubKey&>(pub).data (), const_cast<AccountSig&>(signature).data ());
    }

    BlockHash GetHash () const
    {
        return degest;
    }

    uint16_t GetNumTransactions() const
    {
        return trans.size();
    }

    std::string SerializeJson(bool with_batch_hash = false, bool with_work = false) const
    {
        boost::property_tree::ptree tree;
        SerializeJson (tree, with_batch_hash, with_work);
        std::stringstream ostream;
        boost::property_tree::write_json(ostream, tree);
        return ostream.str();
    }

    void SerializeJson(boost::property_tree::ptree & tree, bool with_batch_hash = false, bool with_work = false) const
    {
        tree.put("account", account.to_string());
        tree.put("previous", previous.to_string());
        tree.put("sequence", std::to_string(sequence));
        tree.put("transaction_type", Type2Str(type));
        tree.put("transaction_fee", transaction_fee.to_string());
        tree.put("signature", signature.to_string());
        if(with_work)
            tree.put("work", std::to_string(work));
        tree.put("number_transactions", std::to_string(trans.size()));

        boost::property_tree::ptree ptree_tran_list;
        for (const auto & t : trans) {
            boost::property_tree::ptree ptree_tran;
            ptree_tran.put("target", t.target.to_string());
            ptree_tran.put("amount", t.amount.to_string());
            ptree_tran_list.push_back(std::make_pair("", ptree_tran));
        }
        tree.add_child("transactions", ptree_tran_list);

        tree.put("hash", degest.to_string());

        if(with_batch_hash)
            tree.put("batch_hash", batch_hash.to_string());
    }

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
            s += logos::write(stream, batch_hash);

        return s;
    }

    logos::mdb_val to_mdb_val(std::vector<uint8_t> & buf) const
    {
        {
            logos::vectorstream stream(buf);
            Serialize(stream, true);
        }
        return logos::mdb_val(buf.size(), buf.data());
    }

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
    mutable BlockHash degest;
    mutable BlockHash batch_hash;
};

