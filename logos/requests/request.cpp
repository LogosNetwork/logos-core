#include <logos/requests/request.hpp>

#include <logos/requests/fields.hpp>
#include <logos/lib/blocks.hpp>

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
    else if(data == CHANGE_REP)
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
            ret = CHANGE_REP;
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
        case RequestType::Unknown:
            ret = UNKNOWN;
            break;
    }

    return ret;
}

Request::Request(bool & error,
                 std::basic_streambuf<uint8_t> & stream)
{
    error = logos::read(stream, type);
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
    }
    catch (...)
    {
        error = true;
    }
}

std::string Request::ToJson() const
{
    boost::property_tree::ptree tree = SerializeJson();
    std::stringstream ostream;
    boost::property_tree::write_json(ostream, tree);
    std::string result = ostream.str ();

    return result;
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

auto Request::Hash() const -> BlockHash
{
    BlockHash result;
    blake2b_state hash;

    auto status (blake2b_init (&hash, sizeof (result.bytes)));
    assert (status == 0);

    blake2b_update(&hash, &type, sizeof(type));
    previous.Hash(hash);

    Hash (hash);

    status = blake2b_final (&hash, result.bytes.data (), sizeof(result.bytes));
    assert (status == 0);

    return result;
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

uint8_t Request::StringWireSize(const std::string & s) const
{
    // Length of string plus one
    // byte to denote the length.
    //
    return s.size() + sizeof(uint8_t);
}
