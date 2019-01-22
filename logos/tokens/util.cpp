#include <logos/tokens/util.hpp>

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
    case TokenSetting::Whitelisting:
        name = "Whitelisting";
        break;
    case TokenSetting::ModifyWhitelisting:
        name = "Modify Whitelisting";
        break;
    }

    return name;
}
