#pragma once

#include <logos/request/requests.hpp>
#include <logos/token/requests.hpp>
#include <logos/governance/requests.hpp>

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
    else if(std::is_same<Request, Proxy>::value)
    {
        result = RequestType::Proxy;
    }
    else if(std::is_same<Request, Issuance>::value)
    {
        result = RequestType::Issuance;
    }
    else if(std::is_same<Request, IssueAdditional>::value)
    {
        result = RequestType::IssueAdditional;
    }
    else if(std::is_same<Request, ChangeSetting>::value)
    {
        result = RequestType::ChangeSetting;
    }
    else if(std::is_same<Request, ImmuteSetting>::value)
    {
        result = RequestType::ImmuteSetting;
    }
    else if(std::is_same<Request, Revoke>::value)
    {
        result = RequestType::Revoke;
    }
    else if(std::is_same<Request, AdjustUserStatus>::value)
    {
        result = RequestType::AdjustUserStatus;
    }
    else if(std::is_same<Request, AdjustFee>::value)
    {
        result = RequestType::AdjustFee;
    }
    else if(std::is_same<Request, UpdateIssuerInfo>::value)
    {
        result = RequestType::UpdateIssuerInfo;
    }
    else if(std::is_same<Request, UpdateController>::value)
    {
        result = RequestType::UpdateController;
    }
    else if(std::is_same<Request, Burn>::value)
    {
        result = RequestType::Burn;
    }
    else if(std::is_same<Request, Distribute>::value)
    {
        result = RequestType::Distribute;
    }
    else if(std::is_same<Request, WithdrawFee>::value)
    {
        result = RequestType::WithdrawFee;
    }
    else if(std::is_same<Request, WithdrawLogos>::value)
    {
        result = RequestType::WithdrawLogos;
    }
    else if(std::is_same<Request, TokenSend>::value)
    {
        result = RequestType::TokenSend;
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
