#pragma once

#include <logos/token/common.hpp>
#include <logos/lib/numbers.hpp>
#include <logos/token/util.hpp>
#include <logos/lib/log.hpp>
#include <logos/common.hpp>

#include <bitset>

class TokenIssuance;
class TokenImmuteSetting;
class TokenChangeSetting;

struct TokenAccount : logos::Account
{
    using Settings    = BitField<TOKEN_SETTINGS_COUNT>;
    using EnumType    = std::underlying_type<TokenSetting>::type;
    using Controllers = std::vector<ControllerInfo>;

    TokenAccount() = default;

    TokenAccount(const TokenIssuance & issuance);

    TokenAccount(bool & error, const logos::mdb_val & mdbval);

    TokenAccount(const BlockHash & head,
                 Amount balance,
                 uint64_t modified,
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

    bool IsController(const AccountAddress & account) const;
    bool GetController(const AccountAddress & account, ControllerInfo & controller) const;

    bool FeeSufficient(uint16_t token_total, uint16_t token_fee) const;
    bool SendAllowed(const TokenUserStatus & status,
                     logos::process_return & result) const;
    bool IsAllowed(std::shared_ptr<const Request> request) const;
    bool IsAllowed(std::shared_ptr<const TokenImmuteSetting> immute) const;
    bool IsAllowed(std::shared_ptr<const TokenChangeSetting> change) const;

    void Set(TokenSetting setting, bool value);
    void Set(TokenSetting setting, SettingValue value);
    bool Allowed(TokenSetting setting) const;

    bool IsMutabilitySetting(TokenSetting setting) const;
    TokenSetting GetMutabilitySetting(TokenSetting setting) const;

    static constexpr uint8_t MAX_CONTROLLERS = 10;

    mutable Log  log;
    uint16_t     token_balance     = 0;
    uint16_t     token_fee_balance = 0;
    TokenFeeType fee_type;
    uint16_t     fee_rate;
    std::string  symbol;
    std::string  name;
    std::string  issuer_info;
    Controllers  controllers;
    Settings     settings;
};
