#include <logos/request/utility.hpp>

#include <logos/request/fields.hpp>
#include <logos/lib/utility.hpp>
#include <logos/elections/requests.hpp>
#include <logos/lib/log.hpp>

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
    else if(data == CHANGE_SETTING)
    {
        ret = RequestType::ChangeTokenSetting;
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
    else if(data == WHITELIST)
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
    else if(data == WITHDRAW_FEE)
    {
        ret = RequestType::WithdrawFee;
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
    else if(data == START_REPRESENTING)
    {
        ret = RequestType::StartRepresenting;
    }
    else if(data == STOP_REPRESENTING)
    {
        ret = RequestType::StopRepresenting;
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
        case RequestType::ChangeTokenSetting:
            ret = CHANGE_SETTING;
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
            ret = WHITELIST;
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
        case RequestType::WithdrawFee:
            ret = WITHDRAW_FEE;
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
        case RequestType::StartRepresenting:
            ret = START_REPRESENTING;
            break;
        case RequestType::StopRepresenting:
            ret = STOP_REPRESENTING;
            break;
        case RequestType::Unknown:
            ret = UNKNOWN;
            break;
    }

    return ret;
}

template<typename ...Args>
std::shared_ptr<Request> BuildRequest(RequestType type, Args&& ...args)
{
    std::shared_ptr<Request> result;

    switch(type)
    {
        case RequestType::Send:
            result = std::make_shared<Send>(Send(args...));
            break;
        case RequestType::ChangeRep:
            result = std::make_shared<Change>(Change(args...));
            break;
        case RequestType::IssueTokens:
            result = std::make_shared<TokenIssuance>(TokenIssuance(args...));
            break;
        case RequestType::IssueAdtlTokens:
            result = std::make_shared<TokenIssueAdtl>(TokenIssueAdtl(args...));
            break;
        case RequestType::ChangeTokenSetting:
            result = std::make_shared<TokenChangeSetting>(TokenChangeSetting(args...));
            break;
        case RequestType::ImmuteTokenSetting:
            result = std::make_shared<TokenImmuteSetting>(TokenImmuteSetting(args...));
            break;
        case RequestType::RevokeTokens:
            result = std::make_shared<TokenRevoke>(TokenRevoke(args...));
            break;
        case RequestType::FreezeTokens:
            result = std::make_shared<TokenFreeze>(TokenFreeze(args...));
            break;
        case RequestType::SetTokenFee:
            result = std::make_shared<TokenSetFee>(TokenSetFee(args...));
            break;
        case RequestType::UpdateWhitelist:
            result = std::make_shared<TokenWhitelist>(TokenWhitelist(args...));
            break;
        case RequestType::UpdateIssuerInfo:
            result = std::make_shared<TokenIssuerInfo>(TokenIssuerInfo(args...));
            break;
        case RequestType::UpdateController:
            result = std::make_shared<TokenController>(TokenController(args...));
            break;
        case RequestType::BurnTokens:
            result = std::make_shared<TokenBurn>(TokenBurn(args...));
            break;
        case RequestType::DistributeTokens:
            result = std::make_shared<TokenAccountSend>(TokenAccountSend(args...));
            break;
        case RequestType::WithdrawFee:
            result = std::make_shared<TokenAccountWithdrawFee>(TokenAccountWithdrawFee(args...));
            break;
        case RequestType::SendTokens:
            result = std::make_shared<TokenSend>(TokenSend(args...));
            break;
        case RequestType::AnnounceCandidacy:
            result = std::make_shared<AnnounceCandidacy>(AnnounceCandidacy(args...));
            break;
        case RequestType::RenounceCandidacy:
            result = std::make_shared<RenounceCandidacy>(RenounceCandidacy(args...));
            break;
        case RequestType::ElectionVote:
            result = std::make_shared<ElectionVote>(ElectionVote(args...));
            break;
        case RequestType::StartRepresenting:
            result = std::make_shared<StartRepresenting>(StartRepresenting(args...));
            break;
        case RequestType::StopRepresenting:
            result = std::make_shared<StopRepresenting>(StopRepresenting(args...));
            break;
        case RequestType::Unknown:
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
    Log log;
    error = logos::peek(stream, type);
    if(error)
    {
        LOG_FATAL(log) << "DeserializeRequest - Error getting request type";
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
