#include <logos/request/requests.hpp>

#include <logos/consensus/persistence/request/request_persistence.hpp>
#include <logos/consensus/persistence/reservations.hpp>
#include <logos/request/utility.hpp>
#include <logos/request/fields.hpp>
#include <logos/lib/utility.hpp>
#include <logos/lib/hash.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <blake2/blake2.h>

#include <ed25519-donna/ed25519.h>

#include <numeric>

using boost::multiprecision::uint128_t;
using namespace boost::multiprecision::literals;

Request::Locator::Locator(bool & error,
                          logos::stream & stream)
{
    error = logos::read(stream, hash);
    if(error)
    {
        return;
    }

    error = logos::read(stream, index);
}

uint64_t Request::Locator::Serialize(logos::stream & stream)
{
    uint64_t bytes_written = 0;

    bytes_written += logos::write(stream, hash);
    bytes_written += logos::write(stream, index);

    return bytes_written;
}

Request::Request(RequestType type)
    : type(type)
{}

Request::Request(RequestType type,
                 const AccountAddress & origin,
                 const BlockHash & previous,
                 const Amount & fee,
                 uint32_t sequence)
    : type(type)
    , origin(origin)
    , previous(previous)
    , fee(fee)
    , sequence(sequence)
{}

Request::Request(RequestType type,
                 const AccountAddress & origin,
                 const BlockHash & previous,
                 const Amount & fee,
                 uint32_t sequence,
                 const AccountSig & signature)
    : type(type)
    , origin(origin)
    , previous(previous)
    , fee(fee)
    , sequence(sequence)
    , signature(signature)
{}

Request::Request(bool & error,
                 const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());
    DeserializeDB(error, stream);
}

Request::Request(bool & error,
                 logos::stream & stream)
{
    Deserialize(error, stream);
}

Request::Request(bool & error,
                 const boost::property_tree::ptree & tree)
{
    using namespace request::fields;

    try
    {
        type = GetRequestType(error, tree.get<std::string>(TYPE));
        if(error)
        {
            return;
        }

        error = origin.decode_account(tree.get<std::string>(ORIGIN));
        if(error)
        {
            return;
        }

        error = previous.decode_hex(tree.get<std::string>(PREVIOUS));
        if(error)
        {
            return;
        }

        error = fee.decode_dec(tree.get<std::string>(FEE));
        if(error)
        {
            return;
        }

        sequence = std::stoul(tree.get<std::string>(SEQUENCE));

        Hash();
    }
    catch (...)
    {
        error = true;
    }
}

void Request::SignAndHash(bool & error, boost::property_tree::ptree const & tree)
{

    using namespace request::fields;
    if(tree.find(SIGNATURE)==tree.not_found())
    {
        AccountPrivKey prv;
        error = prv.decode_hex(tree.get<std::string>(PRIVATE_KEY));
        if(error)
        {
            return;
        }
        Sign(prv);
    }
    else
    {
        error = signature.decode_hex(tree.get<std::string>(SIGNATURE));
        if(error)
        {
            return;
        }
        Hash();
    }
}

logos::AccountType Request::GetAccountType() const
{
    return logos::AccountType::LogosAccount;
}

logos::AccountType Request::GetSourceType() const
{
    return logos::AccountType::LogosAccount;
}

AccountAddress Request::GetAccount() const
{
    return origin;
}

AccountAddress Request::GetSource() const
{
    return origin;
}

Amount Request::GetLogosTotal() const
{
    return fee;
}

Amount Request::GetTokenTotal() const
{
    return {0};
}

void Request::Sign(AccountPrivKey const & priv)
{
    Sign(priv, origin);
}

void Request::Sign(AccountPrivKey const & priv, AccountPubKey const & pub)
{
    digest = Hash();

    ed25519_sign(const_cast<BlockHash&>(digest).data(),
                 HASH_SIZE,
                 const_cast<AccountPrivKey&>(priv).data(),
                 const_cast<AccountPubKey&>(pub).data(),
                 signature.data());
}

bool Request::VerifySignature(AccountPubKey const & pub) const
{
    return 0 == ed25519_sign_open(const_cast<BlockHash &>(digest).data(),
                                  HASH_SIZE,
                                  const_cast<AccountPubKey&>(pub).data(),
                                  const_cast<AccountSig&>(signature).data());
}

std::string Request::ToJson() const
{
    boost::property_tree::ptree tree = SerializeJson();

    std::stringstream ostream;
    boost::property_tree::write_json(ostream, tree);

    return ostream.str();
}

uint64_t Request::ToStream(logos::stream & stream, bool with_work) const
{
    auto bytes_written = 0;

    bytes_written += DoSerialize(stream);
    bytes_written += Serialize(stream);

    return bytes_written;
}

logos::mdb_val Request::ToDatabase(std::vector<uint8_t> & buf, bool with_work) const
{
    assert(buf.empty());

    {
        logos::vectorstream stream(buf);

        DoSerialize(stream);
        locator.Serialize(stream);
        Serialize(stream);

        logos::write(stream, next);
    }

    return {buf.size(), buf.data()};
}

void Request::DeserializeDB(bool & error, logos::stream & stream)
{
    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    locator = Locator(error, stream);
}

boost::property_tree::ptree Request::SerializeJson() const
{
    using namespace request::fields;
    boost::property_tree::ptree tree;

    tree.put(TYPE, GetRequestTypeField(type));
    tree.put(ORIGIN, origin.to_account());
    tree.put(PREVIOUS, previous.to_string());
    tree.put(FEE, fee.to_string_dec());
    tree.put(SEQUENCE, std::to_string(sequence));
    tree.put(SIGNATURE, signature.to_string());
    tree.put(NEXT, next.to_string());

    tree.put(HASH, digest.to_string());
    tree.put("request_block_hash", locator.hash.to_string());
    tree.put("request_block_index", locator.index);

    return tree;
}

uint64_t Request::DoSerialize(logos::stream & stream) const
{
    uint64_t bytes_written = 0;

    bytes_written += logos::write(stream, type);
    bytes_written += logos::write(stream, origin);
    bytes_written += logos::write(stream, previous);
    bytes_written += logos::write(stream, fee);
    bytes_written += logos::write(stream, sequence);

    // Signature is serialized by derived
    // classes.

    return bytes_written;
}

// This is only implemented to prevent
// Request from being an abstract class.
uint64_t Request::Serialize(logos::stream & stream) const
{
    return 0;
}

void Request::Deserialize(bool & error, logos::stream & stream)
{
    error = logos::read(stream, type);
    if(error)
    {
        std::cout << "type rror" << std::endl;
        return;
    }

    error = logos::read(stream, origin);
    if(error)
    {
        std::cout << "origin error" << std::endl;
        return;
    }

    error = logos::read(stream, previous);
    if(error)
    {
        std::cout << "previous error" << std::endl;
        return;
    }

    error = logos::read(stream, fee);
    if(error)
    {
        std::cout << "fee error" << std::endl;
        return;
    }

    error = logos::read(stream, sequence);
    if(error)
    {

        std::cout << "sequence error" << std::endl;
        return;
    }

    // Signature is deserialized by derived
    // classes.
}

bool Request::Validate(logos::process_return & result,
                       std::shared_ptr<logos::Account> info) const
{
    return true;
}

bool Request::Validate(logos::process_return & result) const
{
    // Validate the sender account
    //
    if(origin.is_zero())
    {
        result.code = logos::process_result::opened_burn_account;
        return false;
    }

    // Validate the Logos transaction fee
    //
    if(fee.number() < PersistenceManager<R>::MinTransactionFee(type))
    {
        result.code = logos::process_result::insufficient_fee;
        return false;
    }

    return true;
}

BlockHash Request::GetHash() const
{
    return digest;
}

BlockHash Request::Hash() const
{
    return (digest = Blake2bHash(*this));
}

void Request::Hash(blake2b_state & hash) const
{
    blake2b_update(&hash, &type, sizeof(type));
    origin.Hash(hash);
    previous.Hash(hash);
    fee.Hash(hash);
    blake2b_update(&hash, &sequence, sizeof(sequence));
}

uint16_t Request::WireSize() const
{
    return sizeof(type) +
           sizeof(origin.bytes) +
           sizeof(previous.bytes) +
           sizeof(fee.bytes) +
           sizeof(sequence) +
           sizeof(signature.bytes) +
           sizeof(next.bytes);
}

bool Request::operator==(const Request & other) const
{
    return type == other.type &&
           origin == other.origin &&
           previous == other.previous &&
           fee == other.fee &&
           sequence == other.sequence &&
           signature == other.signature &&
           next == other.next;
}

Send::Send()
    : Request(RequestType::Send)
{}

Send::Send(const AccountAddress & account,
           const BlockHash & previous,
           uint32_t sequence,
           const AccountAddress & to,
           const Amount & amount,
           const Amount & transaction_fee,
           const AccountPrivKey & priv,
           const AccountPubKey & pub)
    : Request(RequestType::Send,
              account,
              previous,
              transaction_fee,
              sequence)
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
           const AccountSig & sig)
    : Request(RequestType::Send,
              account,
              previous,
              transaction_fee,
              sequence,
              sig)
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

        SignAndHash(error, tree);
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
        std::cout << "error in base class" << std::endl;
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        std::cout << "error in derived class" << std::endl;
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
                                 [this](const Amount & a, const Transaction & t)
                                 {
                                     if(t.destination == origin)
                                         return a;
                                     else
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

    return tree;
}

uint64_t Send::Serialize(logos::stream & stream) const
{
    uint64_t bytes_written = 0;

    bytes_written += SerializeVector(stream, transactions);
    std::cout << "serialized transactions,size = "
        << transactions.size() << std::endl;
    bytes_written += logos::write(stream, signature);

    return bytes_written;
}

void Send::Deserialize(bool & error, logos::stream & stream)
{
    uint8_t count;
    error = logos::read(stream, count);
    if(error)
    {
        return;
    }
    std::cout << "deserializing transactions, count = " 
        << (int)count << std::endl;

    for(size_t i = 0; i < count; ++i)
    {
        Transaction t(error, stream);
        if(error)
        {
            return;
        }

        transactions.push_back(t);
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
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
    if(error)
    {
        return;
    }

    error = logos::read(stream, next);
}

bool Send::operator==(const Request & other) const
{
    try
    {
        auto derived = dynamic_cast<const Send &>(other);

        return Request::operator==(other) &&
               transactions == derived.transactions;
    }
    catch(...)
    {}

    return false;
}
