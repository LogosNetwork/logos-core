#include <logos/request/send.hpp>

#include <logos/request/utility.hpp>
#include <logos/request/fields.hpp>

#include <numeric>

Send::Send(const AccountAddress & account,
           const BlockHash & previous,
           uint32_t sequence,
           const AccountAddress & to,
           const Amount & amount,
           const Amount & transaction_fee,
           const AccountPrivKey & priv,
           const AccountPubKey & pub,
           uint64_t work)
    : Request(RequestType::Send,
              account,
              previous,
              transaction_fee,
              sequence,
              priv,
              pub)
    , work(work)
{
    transactions.push_back(Transaction(to, amount));
    Hash();
}

Send::Send(AccountAddress const & account,
           BlockHash const & previous,
           uint32_t sequence,
           AccountAddress const & to,
           Amount const & amount,
           Amount const & transaction_fee,
           const AccountSig & sig,
           uint64_t work)
    : Request(RequestType::Send,
              account,
              previous,
              transaction_fee,
              sequence,
              sig)
    , work(work)
{
    transactions.push_back(Transaction(to, amount));
    Hash();
}

Send::Send(bool & error,
           boost::property_tree::ptree const & tree)
   : Request(error, tree)
{
    using namespace request::fields;

    if(error)
    {
        return;
    }

    try
    {
        error = logos::from_string_hex(tree.get<std::string>("work"), work);
        if(error)
        {
            return;
        }

        auto transactions_tree = tree.get_child("transactions");
        for (auto & entry : transactions_tree)
        {
            Transaction t(error, entry.second);
            if(error)
            {
                return;
            }

            error = !AddTransaction(t);
            if(error)
            {
                return;
            }
        }

        Hash();
    }
    catch (...)
    {
        error = true;
    }
}

Send::Send(bool & error,
           logos::stream & stream)
   : Request(error, stream)
{
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

Send::Send(bool & error, const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());
    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

Amount Send::GetLogosTotal() const
{
    auto total = std::accumulate(transactions.begin(), transactions.end(),
        Amount(0),
        [](const Amount & a, const Transaction & t)
        {
            return a + t.amount;
        });

    return total + Request::GetLogosTotal();
}

bool Send::AddTransaction(const AccountAddress & to, const Amount & amount)
{
    return AddTransaction(Transaction(to, amount));
}

bool Send::AddTransaction(const Transaction & transaction)
{
    if(transactions.size() < MAX_TRANSACTIONS)
    {
        transactions.push_back(transaction);
        return true;
    }
    return false;
}

void Send::Hash(blake2b_state & hash) const
{
    Request::Hash(hash);

    for(const auto & t : transactions)
    {
        t.destination.Hash(hash);
        t.amount.Hash(hash);
    }
}

BlockHash Send::GetHash() const
{
    return digest;
}

boost::property_tree::ptree Send::SerializeJson() const
{
    auto tree = Request::SerializeJson();

    tree.put("work", std::to_string(work));
    tree.put("number_transactions", std::to_string(transactions.size()));

    boost::property_tree::ptree transactions_tree;
    for (const auto & t : transactions)
    {
        boost::property_tree::ptree cur_transaction;

        cur_transaction.put("destination", t.destination.to_account());
        cur_transaction.put("amount", t.amount.to_string_dec());

        transactions_tree.push_back(std::make_pair("", cur_transaction));
    }
    tree.add_child("transactions", transactions_tree);

    tree.put("hash", digest.to_string());

    return tree;
}

uint64_t Send::Serialize (logos::stream & stream) const
{
    return SerializeVector(stream, transactions);
}

void Send::Deserialize(bool & error, logos::stream & stream)
{
    uint8_t count;
    error = logos::read(stream, count);
    if(error)
    {
        return;
    }

    for(size_t i = 0; i < count; ++i)
    {
        Transaction t(error, stream);
        if(error)
        {
            return;
        }

        transactions.push_back(t);
    }
}

void Send::DeserializeDB(bool &error, logos::stream &stream)
{
    Request::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}
