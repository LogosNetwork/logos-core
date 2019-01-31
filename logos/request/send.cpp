#include <logos/request/send.hpp>

#include <logos/request/utility.hpp>
#include <logos/request/fields.hpp>

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
              priv,
              pub)
    , account(account)
    , previous(previous)
    , sequence(sequence)
    , transaction_fee(transaction_fee)
    , work(work)
{
    transactions.push_back(Transaction(to, amount));
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
              sig)
    , account(account)
    , previous(previous)
    , sequence(sequence)
    , transaction_fee(transaction_fee)
    , work(work)
{
    transactions.push_back(Transaction(to, amount));
}

Send::Send(bool & error,
           boost::property_tree::ptree const & tree,
           bool with_batch_hash,
           bool with_work)
   : Request(error, tree)
{
    using namespace request::fields;

    if(error)
    {
        return;
    }

    try
    {
        error = account.decode_account(tree.get<std::string>(ACCOUNT));
        if(error)
        {
            return;
        }

        error = previous.decode_hex(tree.get<std::string>(PREVIOUS));
        if(error)
        {
            return;
        }

        sequence = std::stoul(tree.get<std::string>("sequence"));

        error = transaction_fee.decode_dec(tree.get<std::string>("transaction_fee", "0"));
        if(error)
        {
            return;
        }

        error = signature.decode_hex(tree.get<std::string>("signature", "0"));
        if(error)
        {
            return;
        }

        if(with_work)
        {
            error = logos::from_string_hex(tree.get<std::string>("work"), work);
            if(error)
            {
                return;
            }
        }

        if(with_batch_hash)
        {
            error = batch_hash.decode_hex(tree.get<std::string>("batch_hash"));
            if (error)
            {
                auto index_in_batch = std::stoul(tree.get<std::string>("index_in_batch"));

                // TODO: Is a JSON deserialization method
                //       the appropriate place for semantic
                //       error checking?
                error = index_in_batch > CONSENSUS_BATCH_SIZE;
                if(error)
                {
                    return;
                }
            }
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

    }
    catch (...)
    {
        error = true;
    }
}

Send::Send(bool & error,
           logos::stream & stream,
           bool with_batch_hash)
   : Request(error, stream)
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
        index_in_batch = idx;
    }
}

Send::Send(bool & error, const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *> (mdbval.data()), mdbval.size());
    new (this) Send(error, stream, false);
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
    account.Hash(hash);
    previous.Hash(hash);
    blake2b_update(&hash, &sequence, sizeof(sequence));
    transaction_fee.Hash(hash);

    for(const auto & t : transactions)
    {
        t.destination.Hash(hash);
        t.amount.Hash(hash);
    }
}

BlockHash Send::GetHash () const
{
    return digest;
}

boost::property_tree::ptree Send::SerializeJson() const
{
    auto tree = Request::SerializeJson();

    tree.put("account", account.to_account());
    tree.put("previous", previous.to_string());
    tree.put("sequence", std::to_string(sequence));
    tree.put("transaction_fee", transaction_fee.to_string_dec());
    tree.put("signature", signature.to_string());
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
    tree.put("batch_hash", batch_hash.to_string());
    tree.put("index_in_batch", std::to_string(index_in_batch));

    return tree;
}

uint64_t Send::Serialize (logos::stream & stream) const
{
    return  Request::Serialize(stream) +
            logos::write(stream, account) +
            logos::write(stream, previous) +
            logos::write(stream, sequence) +
            SerializeVector(stream, transactions) +
            logos::write(stream, transaction_fee) +
            logos::write(stream, signature);
}
