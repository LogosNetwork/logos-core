#pragma once

#include <logos/token/common.hpp>
#include <logos/lib/numbers.hpp>
#include <logos/token/util.hpp>
#include <logos/lib/log.hpp>
#include <logos/common.hpp>

#include <bitset>

class TokenImmuteSetting;

struct TokenAccount : logos::Account
{
    using Settings    = std::bitset<TOKEN_SETTINGS_COUNT>;
    using EnumType    = std::underlying_type<TokenSetting>::type;
    using Controllers = std::vector<ControllerInfo>;

    TokenAccount(bool & error, const logos::mdb_val & mdbval);

    TokenAccount(const logos::block_hash & head,
                 logos::amount balance,
                 uint16_t token_balance,
                 uint16_t token_fee_balance,
                 uint32_t block_count);

    uint32_t Serialize(logos::stream &) const override;
    bool Deserialize(logos::stream &) override;
    bool operator==(const TokenAccount &) const;
    bool operator!=(const TokenAccount &) const;
    logos::mdb_val to_mdb_val(std::vector<uint8_t> &) const override;

    bool Validate(TokenSetting setting,
                  bool value,
                  logos::process_return & result) const;
    bool IsAllowed(std::shared_ptr<const Request> request) const;
    bool IsAllowed(std::shared_ptr<const TokenImmuteSetting> immute) const;

    void Set(TokenSetting setting, bool value);
    bool Allowed(TokenSetting setting) const;

    bool IsMutabilitySetting(TokenSetting setting) const;
    TokenSetting GetMutabilitySetting(TokenSetting setting) const;

    static constexpr uint8_t MAX_CONTROLLERS = 10;

    mutable Log log;
    uint16_t    token_balance     = 0;
    uint16_t    token_fee_balance = 0;
    Controllers controllers;
    Settings    settings;
};
