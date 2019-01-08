#pragma once

#include <cstdint>
#include <string>

// XXX - Ensure that settings with odd values
//       represent the mutability of the previous
//       setting.
enum class TokenSettings : uint8_t
{
    AddTokens          = 0,
    ModifyAddTokens    = 1,
    Revoke             = 2,
    ModifyRevoke       = 3,
    Freeze             = 4,
    ModifyFreeze       = 5,
    AdjustFee          = 6,
    ModifyAdjustFee    = 7,
    Whitelisting       = 8,
    ModifyWhitelisting = 9
};

std::string TokenSettingName(TokenSettings setting);

const size_t TOKEN_ACCOUNT_SETTINGS_COUNT = 10;
