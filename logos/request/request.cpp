#include <logos/request/request.hpp>

#include <logos/request/fields.hpp>
#include <logos/lib/utility.hpp>
#include <logos/lib/hash.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <blake2/blake2.h>

RequestType GetRequestType(bool &error, std::string data)
{
    using namespace request::fields;

    std::transform(data.begin(), data.end(),
                   data.begin(), ::tolower);

    RequestType ret = RequestType::Unknown;

    if(data == SEND)
    {
        ret = RequestType::Send;
    }
    else if(data == CHANGE)
    {
        ret = RequestType::ChangeRep;
    }
    else if(data == ISSUE_TOKENS)
    {
        ret = RequestType::IssueTokens;
    }
    else if(data == ISSUE_ADTL)
    {
        ret = RequestType::IssueAdtlTokens;
    }
    else if(data == IMMUTE)
    {
        ret = RequestType::ImmuteTokenSetting;
    }
    else if(data == REVOKE)
    {
        ret = RequestType::RevokeTokens;
    }
    else if(data == FREEZE)
    {
        ret = RequestType::FreezeTokens;
    }
    else if(data == SET_FEE)
    {
        ret = RequestType::SetTokenFee;
    }
    else if(data == UPDATE_WHITELIST)
    {
        ret = RequestType::UpdateWhitelist;
    }
    else if(data == UPDATE_INFO)
    {
        ret = RequestType::UpdateIssuerInfo;
    }
    else if(data == UPDATE_CONTROLLER)
    {
        ret = RequestType::UpdateController;
    }
    else if(data == BURN)
    {
        ret = RequestType::BurnTokens;
    }
    else if(data == DISTRIBUTE)
    {
        ret = RequestType::DistributeTokens;
    }
    else if(data == WITHDRAW)
    {
        ret = RequestType::WithdrawTokens;
    }
    else if(data == SEND_TOKENS)
    {
        ret = RequestType::SendTokens;
    }
    else if(data == ANNOUNCE_CANDIDACY)
    {
        ret = RequestType::AnnounceCandidacy;    
    }
    else if(data == RENOUNCE_CANDIDACY)
    {
        ret = RequestType::RenounceCandidacy;
    }
    else if(data == ELECTION_VOTE)
    {
        ret = RequestType::ElectionVote;
    }
    else
    {
        error = true;
    }

    return ret;
}

std::string GetRequestTypeField(RequestType type)
{
    using namespace::request::fields;
    std::string ret;

    switch(type)
    {
        case RequestType::Send:
            ret = SEND;
            break;
        case RequestType::ChangeRep:
            ret = CHANGE;
            break;
        case RequestType::IssueTokens:
            ret = ISSUE_TOKENS;
            break;
        case RequestType::IssueAdtlTokens:
            ret = ISSUE_ADTL;
            break;
        case RequestType::ImmuteTokenSetting:
            ret = IMMUTE;
            break;
        case RequestType::RevokeTokens:
            ret = REVOKE;
            break;
        case RequestType::FreezeTokens:
            ret = FREEZE;
            break;
        case RequestType::SetTokenFee:
            ret = SET_FEE;
            break;
        case RequestType::UpdateWhitelist:
            ret = UPDATE_WHITELIST;
            break;
        case RequestType::UpdateIssuerInfo:
            ret = UPDATE_INFO;
            break;
        case RequestType::UpdateController:
            ret = UPDATE_CONTROLLER;
            break;
        case RequestType::BurnTokens:
            ret = BURN;
            break;
        case RequestType::DistributeTokens:
            ret = DISTRIBUTE;
            break;
        case RequestType::WithdrawTokens:
            ret = WITHDRAW;
            break;
        case RequestType::SendTokens:
            ret = SEND_TOKENS;
            break;
        case RequestType::AnnounceCandidacy:
            ret = ANNOUNCE_CANDIDACY;
            break;
        case RequestType::RenounceCandidacy:
            ret = RENOUNCE_CANDIDACY;
            break;
        case RequestType::ElectionVote:
            ret = ELECTION_VOTE;
            break;
        case RequestType::Unknown:
            ret = UNKNOWN;
            break;
    }

    return ret;
}

Request::Request(RequestType type,
                 const BlockHash & previous)
     : type(type)
     , previous(previous)
{}

Request::Request(bool & error,
                 std::basic_streambuf<uint8_t> & stream)
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

    error = logos::read(stream, previous);
    if(error)
    {
        return;
    }

    error = logos::read(stream, next);
}

Request::Request(bool & error,
                 const boost::property_tree::ptree & tree)
{
    try
    {
        type = GetRequestType(error, tree.get<std::string>("type"));
        if(error)
        {
            return;
        }

        error = previous.decode_hex(tree.get<std::string>("previous"));
        if(error)
        {
            return;
        }

        error = next.decode_hex(tree.get<std::string>("next"));
        if(error)
        {
            return;
        }
    }
    catch (...)
    {
        error = true;
    }
}

Request::Request(bool & error,
                 const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *> (mdbval.data()),
            mdbval.size());

    new (this) Request(error, stream);
}

std::string Request::ToJson() const
{
    boost::property_tree::ptree tree = SerializeJson();

    std::stringstream ostream;
    boost::property_tree::write_json(ostream, tree);

    return ostream.str();
}

boost::property_tree::ptree Request::SerializeJson() const
{
    using namespace request::fields;
    boost::property_tree::ptree tree;

    tree.put(TYPE, GetRequestTypeField(type));
    tree.put(PREVIOUS, previous.to_string());
    tree.put(NEXT, next.to_string());

    return tree;
}

uint64_t Request::Serialize(logos::stream & stream) const
{
    return logos::write(stream, type) +

           // An additional field is added
           // to the stream to denote the
           // total size of the request.
           //
           logos::write(stream, WireSize()) +
           logos::write(stream, previous) +
           logos::write(stream, next);
}

logos::mdb_val Request::SerializeDB(std::vector<uint8_t> & buf) const
{
    assert(buf.empty());

    {
        logos::vectorstream stream(buf);
        Serialize(stream);
    }

    return {buf.size(), buf.data()};
}

BlockHash Request::GetHash() const
{
    // TODO: precompute?
    //
    return Hash();
}

auto Request::Hash() const -> BlockHash
{
    return Blake2bHash(*this);
}

void Request::Hash(blake2b_state & hash) const
{
    blake2b_update(&hash, &type, sizeof(type));
    previous.Hash(hash);
}

uint16_t Request::WireSize() const
{
    return sizeof(type) +

           // An additional field is added
           // to the stream to denote the
           // total size of the request.
           //
           sizeof(uint16_t) +
           sizeof(previous.bytes) +
           sizeof(next.bytes);
}
