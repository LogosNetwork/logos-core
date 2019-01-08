#include <logos/tokens/util.hpp>

std::string TokenSettingName(TokenSettings setting)
{
    std::string name;

    switch(setting)
    {
    case TokenSettings::AddTokens:
        name = "Add Tokens";
        break;
    case TokenSettings::ModifyAddTokens:
        name = "Modify Add Tokens";
        break;
    case TokenSettings::Revoke:
        name = "Revoke";
        break;
    case TokenSettings::ModifyRevoke:
        name = "Modify Revoke";
        break;
    case TokenSettings::Freeze:
        name = "Freeze";
        break;
    case TokenSettings::ModifyFreeze:
        name = "Modify Freeze";
        break;
    case TokenSettings::AdjustFee:
        name = "Adjust Fee";
        break;
    case TokenSettings::ModifyAdjustFee:
        name = "Modify Adjust Fee";
        break;
    case TokenSettings::Whitelisting:
        name = "Whitelisting";
        break;
    case TokenSettings::ModifyWhitelisting:
        name = "Modify Whitelisting";
        break;
    }

    return name;
}
