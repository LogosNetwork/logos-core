#pragma once

#include <logos/token/utility.hpp>
#include <logos/token/common.hpp>
#include <logos/lib/numbers.hpp>
#include <logos/lib/log.hpp>
#include <logos/common.hpp>

#include <bitset>

class Issuance;
class ChangeSetting;

struct TokenAccount : logos::Account
{
    using Settings    = BitField<TOKEN_SETTINGS_COUNT>;
    using EnumType    = std::underlying_type<TokenSetting>::type;
    using Controllers = std::vector<ControllerInfo>;

    TokenAccount();

    TokenAccount(const Issuance & issuance);

    TokenAccount(bool & error, const logos::mdb_val & mdbval);
    TokenAccount(bool & error, logos::stream & stream);

    TokenAccount(const BlockHash & head,
                 Amount balance,
                 uint64_t modified,
                 Amount token_balance,
                 Amount token_fee_balance,
                 uint32_t block_count,
                 const BlockHash & receive_head,
                 uint32_t receive_count);

    uint32_t Serialize(logos::stream &) const override;
    bool Deserialize(logos::stream &) override;
    boost::property_tree::ptree SerializeJson(bool details=false) const;
    bool operator==(const TokenAccount &) const;
    bool operator!=(const TokenAccount &) const;
    logos::mdb_val to_mdb_val(std::vector<uint8_t> &) const override;

    bool Validate(TokenSetting setting,
                  bool value,
                  logos::process_return & result) const;

    bool IsController(const AccountAddress & account) const;
    bool GetController(const AccountAddress & account, ControllerInfo & controller) const;
    Controllers::iterator GetController(const AccountAddress & account);

    bool FeeSufficient(Amount token_total, Amount token_fee) const;
    bool SendAllowed(const TokenUserStatus & status,
                     logos::process_return & result) const;
    bool IsAllowed(std::shared_ptr<const Request> request) const;
    bool IsAllowed(UserStatus status) const;
    bool IsAllowed(std::shared_ptr<const ChangeSetting> change) const;

    void Set(TokenSetting setting, bool value);
    void Set(TokenSetting setting, SettingValue value);
    bool Allowed(TokenSetting setting) const;

    static bool IsMutabilitySetting(TokenSetting setting);
    static TokenSetting GetMutabilitySetting(TokenSetting setting);

    static constexpr uint8_t MAX_CONTROLLERS = 10;

    mutable Log  log;
    Amount       total_supply      = 0;
    Amount       token_balance     = 0;
    Amount       token_fee_balance = 0;
    TokenFeeType fee_type;
    Amount       fee_rate;
    std::string  symbol;
    std::string  name;
    std::string  issuer_info;
    Controllers  controllers;
    Settings     settings;
    BlockHash    issuance_request;
};
