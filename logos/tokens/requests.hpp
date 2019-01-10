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

    std::string    symbol;
    std::string    name;
    uint16_t       total_supply;
    Settings       settings;
    Controllers    controllers;
    std::string    issuer_info;
};

struct TokenIssuanceAdd : TokenAdminRequest
{
    std::string token_id;
    uint16_t    amount;
};

struct TokenChangeSetting : TokenAdminRequest
{
    std::string  token_id;
    TokenSetting setting;
    uint8_t      value;
};

struct TokenImmuteSetting : TokenAdminRequest
{
    std::string  token_id;
    TokenSetting setting;
};

struct TokenRevoke : TokenAdminRequest
{
    std::string    token_id;
    AccountAddress source;
    AccountAddress dest;
    uint16_t       amount;
};

struct TokenFreeze : TokenAdminRequest
{
    std::string    token_id;
    AccountAddress account;
    uint8_t        action;
};

struct TokenSetFee : TokenAdminRequest
{
    std::string  token_id;
    TokenFeeType fee_type;
    uint16_t     fee_rate;
};

struct TokenWhitelistAdmin : TokenAdminRequest
{
    std::string    token_id;
    AccountAddress account;
};

struct TokenIssuerInfo : TokenAdminRequest
{
    std::string token_id;
    std::string new_info;
};

struct TokenController : TokenAdminRequest
{
    std::string      token_id;
    ControllerAction action;
    ControllerInfo   controller;
};

struct TokenBurn : TokenAdminRequest
{
    std::string token_id;
    uint16_t    amount;
};

struct TokenAccountSend : TokenAdminRequest
{
    std::string    token_id;
    AccountAddress dest;
    uint16_t       amount;
};

struct TokenAccountWithdrawFee : TokenAdminRequest
{
    std::string    token_id;
    AccountAddress dest;
    uint16_t       amount;
};

// Token User Requests
//
struct TokenWhitelistUser : logos::Request
{
    std::string  token_id;
};

struct TokenSend : logos::Request
{
    using Transactions = std::vector<TokenTransaction>;

    std::string  token_id;
    Transactions transactions;
    uint16_t     fee;
};
