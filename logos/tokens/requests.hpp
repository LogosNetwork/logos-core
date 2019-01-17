#pragma once

#include <logos/tokens/common.hpp>
#include <logos/lib/numbers.hpp>

#include <bitset>

// Token Admin Requests
//
struct TokenIssuance : TokenAdminRequest
{
    using Settings    = std::bitset<TOKEN_SETTINGS_COUNT>;
    using Controllers = std::vector<ControllerInfo>;

    TokenIssuance(bool & error,
                  std::basic_streambuf<uint8_t> & stream);

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    std::string symbol;
    std::string name;
    uint16_t    total_supply;
    Settings    settings;
    Controllers controllers;
    std::string issuer_info;
};

struct TokenIssueAdd : TokenAdminRequest
{
    TokenIssueAdd(bool & error,
                  std::basic_streambuf<uint8_t> & stream);

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    uint16_t amount;
};

struct TokenChangeSetting : TokenAdminRequest
{
    TokenChangeSetting(bool & error,
                       std::basic_streambuf<uint8_t> & stream);

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    TokenSetting setting;
    SettingValue value;
};

struct TokenImmuteSetting : TokenAdminRequest
{
    TokenImmuteSetting(bool & error,
                       std::basic_streambuf<uint8_t> & stream);

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    TokenSetting setting;
};

struct TokenRevoke : TokenAdminRequest
{
    TokenRevoke(bool & error,
                std::basic_streambuf<uint8_t> & stream);

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    AccountAddress source;
    AccountAddress dest;
    uint16_t       amount;
};

struct TokenFreeze : TokenAdminRequest
{
    TokenFreeze(bool & error,
                std::basic_streambuf<uint8_t> & stream);

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    AccountAddress account;
    FreezeAction   action;
};

struct TokenSetFee : TokenAdminRequest
{
    TokenSetFee(bool & error,
                std::basic_streambuf<uint8_t> & stream);

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    TokenFeeType fee_type;
    uint16_t     fee_rate;
};

struct TokenWhitelistAdmin : TokenAdminRequest
{
    TokenWhitelistAdmin(bool & error,
                        std::basic_streambuf<uint8_t> & stream);

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    AccountAddress account;
};

struct TokenIssuerInfo : TokenAdminRequest
{
    TokenIssuerInfo(bool & error,
                    std::basic_streambuf<uint8_t> & stream);

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    std::string new_info;
};

struct TokenController : TokenAdminRequest
{
    TokenController(bool & error,
                    std::basic_streambuf<uint8_t> & stream);

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    ControllerAction action;
    ControllerInfo   controller;
};

struct TokenBurn : TokenAdminRequest
{
    TokenBurn(bool & error,
              std::basic_streambuf<uint8_t> & stream);

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    uint16_t amount;
};

struct TokenAccountSend : TokenAdminRequest
{
    TokenAccountSend(bool & error,
                     std::basic_streambuf<uint8_t> & stream);

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    AccountAddress dest;
    uint16_t       amount;
};

struct TokenAccountWithdrawFee : TokenAdminRequest
{
    TokenAccountWithdrawFee(bool & error,
                            std::basic_streambuf<uint8_t> & stream);

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    AccountAddress dest;
    uint16_t       amount;
};

// Token User Requests
//
struct TokenSend : TokenRequest
{
    TokenSend(bool & error,
              std::basic_streambuf<uint8_t> & stream);

    using Transactions = std::vector<TokenTransaction>;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    Transactions transactions;
    uint16_t     fee;
};
