#include <logos/request/request.hpp>

#include <logos/consensus/persistence/reservations.hpp>
#include <logos/request/utility.hpp>
#include <logos/request/fields.hpp>
#include <logos/lib/utility.hpp>
#include <logos/lib/hash.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <blake2/blake2.h>

#include <ed25519-donna/ed25519.h>

using boost::multiprecision::uint128_t;
using namespace boost::multiprecision::literals;

constexpr uint128_t MIN_TRANSACTION_FEE = 0x21e19e0c9bab2400000_cppui128; // 10^22

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
    return logos::write(stream, hash) +
           logos::write(stream, index);
}

Request::Request(RequestType type,
                 const AccountAddress & origin,
                 const BlockHash & previous,
                 const Amount & fee,
                 uint32_t sequence,
                 const AccountPrivKey & priv,
                 const AccountPubKey & pub)
    : type(type)
    , origin(origin)
    , previous(previous)
    , fee(fee)
    , sequence(sequence)
{
    Sign(priv, pub);
}

Request::Request(RequestType type,
                 const AccountAddress & origin,
                 const BlockHash & previous,
                 const Amount & fee,
                 uint32_t sequence,
                 const AccountSig & signature)
    : type(type)
    , origin(origin)
    , signature(signature)
    , previous(previous)
    , fee(fee)
    , sequence(sequence)
{}

Request::Request(bool & error,
                 const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());
    DeserializeDB(error, stream);
    Hash();
}

Request::Request(bool & error,
                 logos::stream & stream)
{
    DoDeserialize(error, stream);
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

        error = signature.decode_hex(tree.get<std::string>(SIGNATURE));
        if(error)
        {
            return;
        }

        error = previous.decode_hex(tree.get<std::string>(PREVIOUS));
        if(error)
        {
            return;
        }

        error = next.decode_hex(tree.get<std::string>(NEXT));
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

logos::AccountType Request::GetAccountType() const
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

uint16_t Request::GetTokenTotal() const
{
    return {0};
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

uint64_t Request::ToStream(logos::stream & stream) const
{
    return DoSerialize(stream) +
           Serialize(stream);
}

logos::mdb_val Request::ToDatabase(std::vector<uint8_t> & buf) const
{
    assert(buf.empty());

    {
        logos::vectorstream stream(buf);

        DoSerialize(stream);
        locator.Serialize(stream);
        Serialize(stream);
    }

    return {buf.size(), buf.data()};
}

void Request::DeserializeDB(bool &error, logos::stream &stream)
{
    DoDeserialize(error, stream);
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
    tree.put(SIGNATURE, signature.to_string());
    tree.put(PREVIOUS, previous.to_string());
    tree.put(NEXT, next.to_string());
    tree.put(FEE, fee.to_string_dec());
    tree.put(SEQUENCE, std::to_string(sequence));

    return tree;
}

uint64_t Request::DoSerialize(logos::stream & stream) const
{
    return logos::write(stream, type) +

           // An additional field is added
           // to the stream to denote the
           // total size of the request.
           //
           logos::write(stream, WireSize()) +
           logos::write(stream, origin) +
           logos::write(stream, signature) +
           logos::write(stream, previous) +
           logos::write(stream, next) +
           logos::write(stream, fee) +
           logos::write(stream, sequence);
}

// This is only implemented to prevent
// Request from being an abstract class.
uint64_t Request::Serialize(logos::stream & stream) const
{
    return 0;
}

void Request::DoDeserialize(bool & error, logos::stream & stream)
{
    error = logos::read(stream, type);
    if(error)
    {
        return;
    }

    uint16_t size;
    error = logos::read(stream, size);
    if(error)
    {
        return;
    }

    error = logos::read(stream, origin);
    if(error)
    {
        return;
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    error = logos::read(stream, previous);
    if(error)
    {
        return;
    }

    error = logos::read(stream, next);
    if(error)
    {
        return;
    }

    error = logos::read(stream, fee);
    if(error)
    {
        return;
    }

    error = logos::read(stream, sequence);
}

void Request::Deserialize(bool & error, logos::stream & stream)
{
    DoDeserialize(error, stream);
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
    if(fee.number() < MIN_TRANSACTION_FEE)
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
    previous.Hash(hash);
    origin.Hash(hash);
    fee.Hash(hash);
    blake2b_update(&hash, &sequence, sizeof(sequence));
}

uint16_t Request::WireSize() const
{
    return sizeof(type) +

           // An additional field is added
           // to the stream to denote the
           // total size of the request.
           //
           sizeof(uint16_t) +
           sizeof(origin.bytes) +
           sizeof(signature.bytes) +
           sizeof(previous.bytes) +
           sizeof(next.bytes) +
           sizeof(fee.bytes) +
           sizeof(sequence);
}

bool Request::operator==(const Request & other) const
{
    return type == other.type
        && (previous == other.previous)
        && (next == other.next)
        && (origin == other.origin)
        && (fee == other.fee)
        && (sequence == other.sequence)
        && (digest == other.digest);
}
