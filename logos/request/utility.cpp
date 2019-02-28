#include <logos/request/utility.hpp>

#include <logos/request/fields.hpp>
#include <logos/lib/utility.hpp>

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
        ret = RequestType::Change;
    }
    else if(data == ISSUANCE)
    {
        ret = RequestType::Issuance;
    }
    else if(data == ISSUE_ADDITIONAL)
    {
        ret = RequestType::IssueAdditional;
    }
    else if(data == CHANGE_SETTING)
    {
        ret = RequestType::ChangeSetting;
    }
    else if(data == IMMUTE_SETTING)
    {
        ret = RequestType::ImmuteSetting;
    }
    else if(data == REVOKE)
    {
        ret = RequestType::Revoke;
    }
    else if(data == ADJUST_USER_STATUS)
    {
        ret = RequestType::AdjustUserStatus;
    }
    else if(data == ADJUST_FEE)
    {
        ret = RequestType::AdjustFee;
    }
    else if(data == UPDATE_ISSUER_INFO)
    {
        ret = RequestType::UpdateIssuerInfo;
    }
    else if(data == UPDATE_CONTROLLER)
    {
        ret = RequestType::UpdateController;
    }
    else if(data == BURN)
    {
        ret = RequestType::Burn;
    }
    else if(data == DISTRIBUTE)
    {
        ret = RequestType::Distribute;
    }
    else if(data == WITHDRAW_FEE)
    {
        ret = RequestType::WithdrawFee;
    }
    else if(data == TOKEN_SEND)
    {
        ret = RequestType::TokenSend;
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
        case RequestType::Change:
            ret = CHANGE;
            break;
        case RequestType::Issuance:
            ret = ISSUANCE;
            break;
        case RequestType::IssueAdditional:
            ret = ISSUE_ADDITIONAL;
            break;
        case RequestType::ChangeSetting:
            ret = CHANGE_SETTING;
            break;
        case RequestType::ImmuteSetting:
            ret = IMMUTE_SETTING;
            break;
        case RequestType::Revoke:
            ret = REVOKE;
            break;
        case RequestType::AdjustUserStatus:
            ret = ADJUST_USER_STATUS;
            break;
        case RequestType::AdjustFee:
            ret = ADJUST_FEE;
            break;
        case RequestType::UpdateIssuerInfo:
            ret = UPDATE_ISSUER_INFO;
            break;
        case RequestType::UpdateController:
            ret = UPDATE_CONTROLLER;
            break;
        case RequestType::Burn:
            ret = BURN;
            break;
        case RequestType::Distribute:
            ret = DISTRIBUTE;
            break;
        case RequestType::WithdrawFee:
            ret = WITHDRAW_FEE;
            break;
        case RequestType::TokenSend:
            ret = TOKEN_SEND;
            break;
        case RequestType::Unknown:
            ret = UNKNOWN;
            break;
    }

    return ret;
}

template<typename Data>
std::shared_ptr<Request> BuildRequest(RequestType type, bool & error, Data && data)
{
    std::shared_ptr<Request> result;

    switch(type)
    {
        case RequestType::Send:
            result = std::make_shared<Send>(error, data);
            break;
        case RequestType::Change:
            result = std::make_shared<Change>(error, data);
            break;
        case RequestType::Issuance:
            result = std::make_shared<Issuance>(error, data);
            break;
        case RequestType::IssueAdditional:
            result = std::make_shared<IssueAdditional>(error, data);
            break;
        case RequestType::ChangeSetting:
            result = std::make_shared<ChangeSetting>(error, data);
            break;
        case RequestType::ImmuteSetting:
            result = std::make_shared<ImmuteSetting>(error, data);
            break;
        case RequestType::Revoke:
            result = std::make_shared<Revoke>(error, data);
            break;
        case RequestType::AdjustUserStatus:
            result = std::make_shared<AdjustUserStatus>(error, data);
            break;
        case RequestType::AdjustFee:
            result = std::make_shared<AdjustFee>(error, data);
            break;
        case RequestType::UpdateIssuerInfo:
            result = std::make_shared<UpdateIssuerInfo>(error, data);
            break;
        case RequestType::UpdateController:
            result = std::make_shared<UpdateController>(error, data);
            break;
        case RequestType::Burn:
            result = std::make_shared<Burn>(error, data);
            break;
        case RequestType::Distribute:
            result = std::make_shared<Distribute>(error, data);
            break;
        case RequestType::WithdrawFee:
            result = std::make_shared<WithdrawFee>(error, data);
            break;
        case RequestType::TokenSend:
            result = std::make_shared<TokenSend>(error, data);
            break;
        case RequestType::Unknown:
            error = true;
            break;
    }

    return result;
}

std::shared_ptr<Request> DeserializeRequest(bool & error, const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    RequestType type = RequestType::Unknown;

    error = logos::read(stream, type);
    if(error)
    {
        return {nullptr};
    }

    return BuildRequest(type, error, mdbval);
}

std::shared_ptr<Request> DeserializeRequest(bool & error, logos::stream & stream)
{
    RequestType type;

    error = logos::peek(stream, type);
    if(error)
    {
        return {nullptr};
    }

    return BuildRequest(type, error, stream);
}

std::shared_ptr<Request> DeserializeRequest(bool & error, boost::property_tree::ptree & tree)
{
    using namespace request::fields;

    RequestType type = GetRequestType(error, tree.get<std::string>(TYPE, UNKNOWN));
    if(error)
    {
        return {nullptr};
    }

    return BuildRequest(type, error, tree);
}