#pragma once

#include <logos/tokens/util.hpp>
#include <logos/lib/log.hpp>
#include <logos/common.hpp>

#include <bitset>

class TokenAccount
{
    using Settings = std::bitset<TOKEN_ACCOUNT_SETTINGS_COUNT>;
    using EnumType = std::underlying_type<TokenSettings>::type;

public:

    bool Validate(TokenSettings setting,
                  bool value,
                  logos::process_return & result) const;

    void Set(TokenSettings setting, bool value);
    bool Allowed(TokenSettings setting) const;

private:

    bool IsMutabilitySetting(TokenSettings setting) const;
    TokenSettings GetMutabilitySetting(TokenSettings setting) const;

    mutable Log _log;
    Settings    _settings;
};
