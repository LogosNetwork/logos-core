#pragma once

#include <logos/lib/numbers.hpp>
#include <logos/tokens/util.hpp>
#include <logos/lib/log.hpp>
#include <logos/common.hpp>

#include <bitset>

class TokenAccount
{
    using Settings = std::bitset<TOKEN_SETTINGS_COUNT>;
    using EnumType = std::underlying_type<TokenSetting>::type;

public:

    bool Validate(TokenSetting setting,
                  bool value,
                  logos::process_return & result) const;

    void Set(TokenSetting setting, bool value);
    bool Allowed(TokenSetting setting) const;

private:

    bool IsMutabilitySetting(TokenSetting setting) const;
    TokenSetting GetMutabilitySetting(TokenSetting setting) const;

    mutable Log _log;
    BlockHash   _head;
    Settings    _settings;
};
