#include <logos/request/send.hpp>

Send::Send(AccountAddress const & account,
           BlockHash const & previous,
           uint32_t sequence,
           AccountAddress const & to,
           Amount const & amount,
           Amount const & transaction_fee,
           AccountPrivKey const & priv,
           AccountPubKey const & pub,
           uint64_t work)
    : Request(RequestType::Send,
              previous)
    , account(account)
    , previous(previous)
    , sequence(sequence)
    , transactions()
    , transaction_fee(transaction_fee)
    , signature()
    , work(work)
{
    transactions.push_back(Transaction(to, amount));
    Sign(priv, pub);
}

Send::Send(AccountAddress const & account,
           BlockHash const & previous,
           uint32_t sequence,
           AccountAddress const & to,
           Amount const & amount,
           Amount const & transaction_fee,
           AccountSig const & sig,
           uint64_t work)
    : Request(RequestType::Send,
              previous)
    , account(account)
    , previous(previous)
    , sequence(sequence)
    , transactions()
    , transaction_fee(transaction_fee)
    , signature(sig)
    , work(work)
{
    transactions.push_back(Transaction(to, amount));
    Hash();
}

Send::Send(bool & error,
           boost::property_tree::ptree const & tree,
           bool with_batch_hash,
           bool with_work)
   : Request(error, tree)
{
    if(error)
    {
        return;
    }

    try
    {
        size_t num_trans = 0;
        auto account_l (tree.get<std::string> ("account"));
        error = account.decode_account (account_l);
        if (!error)
        {
            auto previous_l (tree.get<std::string> ("previous"));
            error = previous.decode_hex (previous_l);
            if (!error)
            {
                auto sequence_l (tree.get<std::string> ("sequence"));
                sequence = std::stoul(sequence_l);
                auto fee_l (tree.get<std::string> ("transaction_fee", "0"));
                error = transaction_fee.decode_dec (fee_l);
                if (!error)
                {
                    auto signature_l (tree.get<std::string> ("signature", "0"));
                    error = signature.decode_hex (signature_l);
                    if (!error)
                    {
                        if(with_work)
                        {
                            auto work_l (tree.get<std::string> ("work"));
                            error = logos::from_string_hex (work_l, work);
                        }
                        if (!error)
                        {
                            if(with_batch_hash)
                            {
                                auto batch_hash_l (tree.get<std::string> ("batch_hash"));
                                error = batch_hash.decode_hex (batch_hash_l);
                                if (!error)
                                {
                                    auto index_in_batch_hash_l (tree.get<std::string> ("index_in_batch"));
                                    auto index_in_batch_ul = std::stoul(index_in_batch_hash_l);
                                    error = index_in_batch_ul > CONSENSUS_BATCH_SIZE;
                                    if( ! error)
                                        index_in_batch = index_in_batch_ul;
                                }
                            }
                            if (!error)
                            {
                                auto trans_count_l (tree.get<std::string> ("number_transactions"));
                                num_trans = std::stoul(trans_count_l);

                                auto trans_tree = tree.get_child("transactions");
                                for (const std::pair<std::string, boost::property_tree::ptree> &p : trans_tree)
                                {
                                    auto amount_l (p.second.get<std::string> ("amount"));
                                    Amount tran_amount;
                                    error = tran_amount.decode_dec (amount_l);
                                    if (!error)
                                    {
                                        auto target_l (p.second.get<std::string> ("target"));
                                        AccountAddress tran_target;
                                        error = tran_target.decode_account (target_l);
                                        if(error)
                                        {
                                            break;
                                        }
                                        error = ! AddTransaction(tran_target, tran_amount);
                                        if(error)
                                        {
                                            break;
                                        }
                                        error = (GetNumTransactions() > num_trans);
                                        if(error)
                                        {
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    catch (std::runtime_error const &)
    {
        error = true;
    }

    if (!error)
    {
        Hash();
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

    Hash ();
}

Send::Send(bool & error, const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *> (mdbval.data()), mdbval.size());
    new (this) Send(error, stream, false);
}

bool Send::AddTransaction(AccountAddress const & to, Amount const & amount)
{
    if(transactions.size() < MAX_TRANSACTIONS)
    {
        transactions.push_back(Transaction(to, amount));
        return true;
    }
    return false;
}

BlockHash Send::Hash() const
{
    return (digest = Request::Hash());
}

void Send::Hash(blake2b_state & hash) const
{
    account.Hash(hash);
    previous.Hash(hash);
    blake2b_update(&hash, &sequence, sizeof(sequence));
    blake2b_update(&hash, transaction_fee.data(), ACCOUNT_AMOUNT_SIZE);
    uint8_t count = transactions.size();
    blake2b_update(&hash, &count, sizeof(count));

    for (const auto & t : transactions)
    {
        t.target.Hash(hash);
        blake2b_update(&hash, t.amount.data(), ACCOUNT_AMOUNT_SIZE);
    }
}

void Send::Sign(AccountPrivKey const & priv, AccountPubKey const & pub)
{
    digest = Hash();
    ed25519_sign (const_cast<BlockHash &>(digest).data(), HASH_SIZE, const_cast<AccountPrivKey&>(priv).data (), const_cast<AccountPubKey&>(pub).data (), signature.data ());
}

bool Send::VerifySignature(AccountPubKey const & pub) const
{
    return 0 == ed25519_sign_open (const_cast<BlockHash &>(digest).data(), HASH_SIZE, const_cast<AccountPubKey&>(pub).data (), const_cast<AccountSig&>(signature).data ());
}

BlockHash Send::GetHash () const
{
    return digest;
}

uint8_t Send::GetNumTransactions() const
{
    return transactions.size();
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

        cur_transaction.put("target", t.target.to_account());
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
