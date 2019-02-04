#include <logos/token/util.hpp>

#include <logos/request/fields.hpp>

std::string TokenSettingName(TokenSetting setting)
{
    std::string name;

    switch(setting)
    {
    case TokenSetting::AddTokens:
        name = "Add Tokens";
        break;
    case TokenSetting::ModifyAddTokens:
        name = "Modify Add Tokens";
        break;
    case TokenSetting::Revoke:
        name = "Revoke";
        break;
    case TokenSetting::ModifyRevoke:
        name = "Modify Revoke";
        break;
    case TokenSetting::Freeze:
        name = "Freeze";
        break;
    case TokenSetting::ModifyFreeze:
        name = "Modify Freeze";
        break;
    case TokenSetting::AdjustFee:
        name = "Adjust Fee";
        break;
    case TokenSetting::ModifyAdjustFee:
        name = "Modify Adjust Fee";
        break;
    case TokenSetting::Whitelist:
        name = "Whitelist";
        break;
    case TokenSetting::ModifyWhitelist:
        name = "Modify Whitelist";
        break;
    case TokenSetting::Unknown:
        name = "Unknown";
        break;
    }

    return name;
}

TokenSetting GetTokenSetting(bool & error, std::string data)
{
    using namespace::request::fields;

    std::transform(data.begin(), data.end(),
                   data.begin(), ::tolower);

    TokenSetting ret = TokenSetting::Unknown;

    if(data == ADD)
    {
        ret = TokenSetting::AddTokens;
    }
    else if(data == MODIFY_ADD)
    {
        ret = TokenSetting::ModifyAddTokens;
    }
    else if(data == REVOKE)
    {
        ret = TokenSetting::Revoke;
    }
    else if(data == MODIFY_REVOKE)
    {
        ret = TokenSetting::ModifyRevoke;
    }
    else if(data == FREEZE)
    {
        ret = TokenSetting::Freeze;
    }
    else if(data == MODIFY_FREEZE)
    {
        ret = TokenSetting::ModifyFreeze;
    }
    else if(data == ADJUST_FEE)
    {
        ret = TokenSetting::AdjustFee;
    }
    else if(data == MODIFY_ADJUST_FEE)
    {
        ret = TokenSetting::ModifyAdjustFee;
    }
    else if(data == WHITELIST)
    {
        ret = TokenSetting::Whitelist;
    }
    else if(data == MODIFY_WHITELIST)
    {
        ret = TokenSetting::ModifyWhitelist;
    }
    else
    {
        error = true;
    }

    return ret;
}

std::string GetTokenSettingField(size_t pos)
{
    return GetTokenSettingField(static_cast<TokenSetting>(pos));
}

std::string GetTokenSettingField(TokenSetting setting)
{
    using namespace::request::fields;

    std::string ret;

    switch(setting)
    {
        case TokenSetting::AddTokens:
            ret = ADD;
            break;
        case TokenSetting::ModifyAddTokens:
            ret = MODIFY_ADD;
            break;
        case TokenSetting::Revoke:
            ret = REVOKE;
            break;
        case TokenSetting::ModifyRevoke:
            ret = MODIFY_REVOKE;
            break;
        case TokenSetting::Freeze:
            ret = FREEZE;
            break;
        case TokenSetting::ModifyFreeze:
            ret = MODIFY_FREEZE;
            break;
        case TokenSetting::AdjustFee:
            ret = ADJUST_FEE;
            break;
        case TokenSetting::ModifyAdjustFee:
            ret = MODIFY_ADJUST_FEE;
            break;
        case TokenSetting::Whitelist:
            ret = WHITELIST;
            break;
        case TokenSetting::ModifyWhitelist:
            ret = MODIFY_WHITELIST;
            break;
        case TokenSetting::Unknown:
            ret = UNKNOWN;
            break;
    }

    return ret;
}

ControllerPrivilege GetControllerPrivilege(bool & error, std::string data)
{
    using namespace::request::fields;

    std::transform(data.begin(), data.end(),
                   data.begin(), ::tolower);

    ControllerPrivilege ret = ControllerPrivilege::Unknown;

    if(data == CHANGE_ADD)
    {
        ret = ControllerPrivilege::ChangeAddTokens;
    }
    else if(data == CHANGE_MODIFY_ADD)
    {
        ret = ControllerPrivilege::ChangeModifyAddTokens;
    }
    else if(data == CHANGE_REVOKE)
    {
        ret = ControllerPrivilege::ChangeRevoke;
    }
    else if(data == CHANGE_MODIFY_REVOKE)
    {
        ret = ControllerPrivilege::ChangeModifyRevoke;
    }
    else if(data == CHANGE_FREEZE)
    {
        ret = ControllerPrivilege::ChangeFreeze;
    }
    else if(data == CHANGE_MODIFY_FREEZE)
    {
        ret = ControllerPrivilege::ChangeModifyFreeze;
    }
    else if(data == CHANGE_ADJUST_FEE)
    {
        ret = ControllerPrivilege::ChangeAdjustFee;
    }
    else if(data == CHANGE_MODIFY_ADJUST_FEE)
    {
        ret = ControllerPrivilege::ChangeModifyAdjustFee;
    }
    else if(data == CHANGE_WHITELIST)
    {
        ret = ControllerPrivilege::ChangeWhitelist;
    }
    else if(data == CHANGE_MODIFY_WHITELIST)
    {
        ret = ControllerPrivilege::ChangeModifyWhitelist;
    }
    else if(data == PROMOTE_CONTROLLER)
    {
        ret = ControllerPrivilege::PromoteController;
    }
    else if(data == ADD)
    {
        ret = ControllerPrivilege::AddTokens;
    }
    else if(data == REVOKE)
    {
        ret = ControllerPrivilege::Revoke;
    }
    else if(data == FREEZE)
    {
        ret = ControllerPrivilege::Freeze;
    }
    else if(data == ADJUST_FEE)
    {
        ret = ControllerPrivilege::AdjustFee;
    }
    else if(data == WHITELIST)
    {
        ret = ControllerPrivilege::Whitelist;
    }
    else if(data == BURN)
    {
        ret = ControllerPrivilege::Burn;
    }
    else if(data == WITHDRAW)
    {
        ret = ControllerPrivilege::Withdraw;
    }
    else if(data == WITHDRAW_FEE)
    {
        ret = ControllerPrivilege::WithdrawFee;
    }
    else
    {
        error = true;
    }

    return ret;
}

std::string GetControllerPrivilegeField(size_t pos)
{
    return GetControllerPrivilegeField(static_cast<ControllerPrivilege>(pos));
}

std::string GetControllerPrivilegeField(ControllerPrivilege privilege)
{
    using namespace::request::fields;

    std::string ret;

    switch(privilege)
    {
        case ControllerPrivilege::ChangeAddTokens:
            ret = CHANGE_ADD;
            break;
        case ControllerPrivilege::ChangeModifyAddTokens:
            ret = CHANGE_MODIFY_ADD;
            break;
        case ControllerPrivilege::ChangeRevoke:
            ret = CHANGE_REVOKE;
            break;
        case ControllerPrivilege::ChangeModifyRevoke:
            ret = CHANGE_MODIFY_REVOKE;
            break;
        case ControllerPrivilege::ChangeFreeze:
            ret = CHANGE_FREEZE;
            break;
        case ControllerPrivilege::ChangeModifyFreeze:
            ret = CHANGE_MODIFY_FREEZE;
            break;
        case ControllerPrivilege::ChangeAdjustFee:
            ret = CHANGE_ADJUST_FEE;
            break;
        case ControllerPrivilege::ChangeModifyAdjustFee:
            ret = CHANGE_MODIFY_ADJUST_FEE;
            break;
        case ControllerPrivilege::ChangeWhitelist:
            ret = CHANGE_WHITELIST;
            break;
        case ControllerPrivilege::ChangeModifyWhitelist:
            ret = CHANGE_MODIFY_WHITELIST;
            break;
        case ControllerPrivilege::PromoteController:
            ret = PROMOTE_CONTROLLER;
            break;
        case ControllerPrivilege::AddTokens:
            ret = ADD;
            break;
        case ControllerPrivilege::Revoke:
            ret = REVOKE;
            break;
        case ControllerPrivilege::Freeze:
            ret = FREEZE;
            break;
        case ControllerPrivilege::AdjustFee:
            ret = ADJUST_FEE;
            break;
        case ControllerPrivilege::Whitelist:
            ret = WHITELIST;
            break;
        case ControllerPrivilege::UpdateIssuerInfo:
            ret = UPDATE_ISSUER_INFO;
            break;
        case ControllerPrivilege::Burn:
            ret = BURN;
            break;
        case ControllerPrivilege::Withdraw:
            ret = WITHDRAW;
            break;
        case ControllerPrivilege::WithdrawFee:
            ret = WITHDRAW_FEE;
            break;
        case ControllerPrivilege::Unknown:
            ret = UNKNOWN;
            break;
    }

    return ret;
}

TokenFeeType GetTokenFeeType(bool & error, std::string data)
{
    using namespace::request::fields;

    std::transform(data.begin(), data.end(),
                   data.begin(), ::tolower);

    TokenFeeType ret = TokenFeeType::Unknown;

    if(data == PERCENTAGE)
    {
        ret = TokenFeeType::Percentage;
    }
    else if(data == FLAT)
    {
        ret = TokenFeeType::Flat;
    }
    else
    {
        error = true;
    }

    return ret;
}

std::string GetTokenFeeTypeField(TokenFeeType fee_type)
{
    using namespace::request::fields;

    std::string ret;

    switch(fee_type)
    {
        case TokenFeeType::Percentage:
            ret = PERCENTAGE;
            break;
        case TokenFeeType::Flat:
            ret = FLAT;
            break;
        case TokenFeeType::Unknown:
            break;
    }

    return ret;
}

ControllerAction GetControllerAction(bool & error, std::string data)
{
    using namespace::request::fields;

    std::transform(data.begin(), data.end(),
                   data.begin(), ::tolower);

    ControllerAction ret = ControllerAction::Unknown;

    if(data == ADD)
    {
        ret = ControllerAction::Add;
    }
    else if(data == REMOVE)
    {
        ret = ControllerAction::Remove;
    }
    else
    {
        error = true;
    }

    return ret;
}

std::string GetControllerActionField(ControllerAction action)
{
    using namespace::request::fields;

    std::string ret;

    switch(action)
    {
        case ControllerAction::Add:
            ret = ADD;
            break;
        case ControllerAction::Remove:
            ret = REMOVE;
            break;
        case ControllerAction::Unknown:
            break;
    }

    return ret;
}

FreezeAction GetFreezeAction(bool & error, std::string data)
{
    using namespace::request::fields;

    std::transform(data.begin(), data.end(),
                   data.begin(), ::tolower);

    FreezeAction ret = FreezeAction::Unknown;

    if(data == FREEZE)
    {
        ret = FreezeAction::Freeze;
    }
    else if(data == UNFREEZE)
    {
        ret = FreezeAction::Unfreeze;
    }
    else
    {
        error = true;
    }

    return ret;
}

std::string GetFreezeActionField(FreezeAction action)
{
    using namespace::request::fields;

    std::string ret;

    switch(action)
    {
    case FreezeAction::Freeze:
        ret = FREEZE;
        break;
    case FreezeAction::Unfreeze:
        ret = UNFREEZE;
        break;
    case FreezeAction::Unknown:
        break;
    }

    return ret;
}

bool IsTokenAdminRequest(RequestType type)
{
    bool result = false;

    switch(type)
    {
    case RequestType::Send:
    case RequestType::ChangeRep:
    case RequestType::IssueTokens:
        break;
    case RequestType::IssueAdtlTokens:
    case RequestType::ChangeTokenSetting:
    case RequestType::ImmuteTokenSetting:
    case RequestType::RevokeTokens:
    case RequestType::FreezeTokens:
    case RequestType::SetTokenFee:
    case RequestType::UpdateWhitelist:
    case RequestType::UpdateIssuerInfo:
    case RequestType::UpdateController:
    case RequestType::BurnTokens:
    case RequestType::DistributeTokens:
    case RequestType::WithdrawTokens:
        result = true;
        break;
    case RequestType::SendTokens:
    case RequestType::Unknown:
        break;
    }

    return result;
}
