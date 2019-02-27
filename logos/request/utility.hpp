#pragma once

#include <logos/request/request.hpp>
#include <logos/request/change.hpp>
#include <logos/token/requests.hpp>
#include <logos/request/send.hpp>
#include <logos/elections/requests.hpp>

RequestType GetRequestType(bool &error, std::string data);
std::string GetRequestTypeField(RequestType type);

std::shared_ptr<Request> DeserializeRequest(bool & error, const logos::mdb_val & mdbval);
std::shared_ptr<Request> DeserializeRequest(bool & error, logos::stream & stream);
std::shared_ptr<Request> DeserializeRequest(bool & error, boost::property_tree::ptree & tree);

template<typename T>
uint16_t VectorWireSize(const std::vector<T> & v)
{
    // The size of the vector's
    // elements plus the size
    // of the field denoting
    // the number of elements.
    //
    return (T::WireSize() * v.size()) + sizeof(uint8_t);
}

template<typename T = uint8_t>
T StringWireSize(const std::string & s)
{
    static_assert(std::is_integral<T>::value,
                  "Integral type required.");

    assert(s.size() <= std::numeric_limits<T>::max());

    // Length of string plus one
    // byte to denote the length.
    //
    return s.size() + sizeof(T);
}

template<typename T, typename S = uint8_t>
uint64_t SerializeVector(logos::stream & stream, const std::vector<T> & v)
{
    static_assert(std::is_integral<S>::value,
                  "Integral type required.");

    assert(v.size() < std::numeric_limits<S>::max());

    uint64_t written = logos::write(stream, S(v.size()));

    for(size_t i = 0; i < v.size(); ++i)
    {
        written += v[i].Serialize(stream);
    }

    return written;
}

template<typename Request>
RequestType GetRequestType()
{
    RequestType result = RequestType::Unknown;

    if(std::is_same<Request, Send>::value)
    {
        result = RequestType::Send;
    }
    else if(std::is_same<Request, Change>::value)
    {
        result = RequestType::ChangeRep;
    }
    else if(std::is_same<Request, TokenIssuance>::value)
    {
        result = RequestType::IssueTokens;
    }
    else if(std::is_same<Request, TokenIssueAdtl>::value)
    {
        result = RequestType::IssueAdtlTokens;
    }
    else if(std::is_same<Request, TokenChangeSetting>::value)
    {
        result = RequestType::ChangeTokenSetting;
    }
    else if(std::is_same<Request, TokenImmuteSetting>::value)
    {
        result = RequestType::ImmuteTokenSetting;
    }
    else if(std::is_same<Request, TokenRevoke>::value)
    {
        result = RequestType::RevokeTokens;
    }
    else if(std::is_same<Request, TokenFreeze>::value)
    {
        result = RequestType::FreezeTokens;
    }
    else if(std::is_same<Request, TokenSetFee>::value)
    {
        result = RequestType::SetTokenFee;
    }
    else if(std::is_same<Request, TokenWhitelist>::value)
    {
        result = RequestType::UpdateWhitelist;
    }
    else if(std::is_same<Request, TokenIssuerInfo>::value)
    {
        result = RequestType::UpdateIssuerInfo;
    }
    else if(std::is_same<Request, TokenController>::value)
    {
        result = RequestType::UpdateController;
    }
    else if(std::is_same<Request, TokenBurn>::value)
    {
        result = RequestType::BurnTokens;
    }
    else if(std::is_same<Request, TokenAccountSend>::value)
    {
        result = RequestType::DistributeTokens;
    }
    else if(std::is_same<Request, TokenAccountWithdrawFee>::value)
    {
        result = RequestType::WithdrawFee;
    }
    else if(std::is_same<Request, TokenSend>::value)
    {
        result = RequestType::SendTokens;
    }
    else if(std::is_same<Request, ElectionVote>::value)
    {
        result = RequestType::ElectionVote;
    }
    else if(std::is_same<Request, AnnounceCandidacy>::value)
    {
        result = RequestType::AnnounceCandidacy;
    }
    else if(std::is_same<Request, RenounceCandidacy>::value)
    {
        result = RequestType::RenounceCandidacy;
    }
    else if(std::is_same<Request, StartRepresenting>::value)
    {
        result = RequestType::StartRepresenting;
    }
    else if(std::is_same<Request, StopRepresenting>::value)
    {
        result = RequestType::StopRepresenting;
    }
    return result;
}
