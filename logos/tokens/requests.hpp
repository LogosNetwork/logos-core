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

    void Hash(blake2b_state & hash) const override;

    std::string symbol;
    std::string name;
    uint16_t    total_supply;
    Settings    settings;
    Controllers controllers;
    std::string issuer_info;
};

struct TokenIssueAdd : TokenAdminRequest
{
    void Hash(blake2b_state & hash) const override;

    uint16_t amount;
};

struct TokenChangeSetting : TokenAdminRequest
{
    void Hash(blake2b_state & hash) const override;

    TokenSetting setting;
    uint8_t      value;
};

struct TokenImmuteSetting : TokenAdminRequest
{
    void Hash(blake2b_state & hash) const override;

    TokenSetting setting;
};

struct TokenRevoke : TokenAdminRequest
{
    void Hash(blake2b_state & hash) const override;

    AccountAddress source;
    AccountAddress dest;
    uint16_t       amount;
};

struct TokenFreeze : TokenAdminRequest
{
    void Hash(blake2b_state & hash) const override;

    AccountAddress account;
    uint8_t        action;
};

struct TokenSetFee : TokenAdminRequest
{
    void Hash(blake2b_state & hash) const override;

    TokenFeeType fee_type;
    uint16_t     fee_rate;
};

struct TokenWhitelistAdmin : TokenAdminRequest
{
    void Hash(blake2b_state & hash) const override;

    AccountAddress account;
};

struct TokenIssuerInfo : TokenAdminRequest
{
    void Hash(blake2b_state & hash) const override;

    std::string new_info;
};

struct TokenController : TokenAdminRequest
{
    void Hash(blake2b_state & hash) const override;

    ControllerAction action;
    ControllerInfo   controller;
};

struct TokenBurn : TokenAdminRequest
{
    void Hash(blake2b_state & hash) const override;

    uint16_t amount;
};

struct TokenAccountSend : TokenAdminRequest
{
    void Hash(blake2b_state & hash) const override;

    AccountAddress dest;
    uint16_t       amount;
};

struct TokenAccountWithdrawFee : TokenAdminRequest
{
    void Hash(blake2b_state & hash) const override;

    AccountAddress dest;
    uint16_t       amount;
};

// Token User Requests
//
struct TokenWhitelistUser : logos::Request
{
    void Hash(blake2b_state & hash) const override;
};

struct TokenSend : logos::Request
{
    using Transactions = std::vector<TokenTransaction>;

    void Hash(blake2b_state & hash) const override;

    Transactions transactions;
    uint16_t     fee;
};
