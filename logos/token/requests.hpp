#pragma once

#include <logos/request/transaction.hpp>
#include <logos/token/common.hpp>
#include <logos/lib/numbers.hpp>

// Token Admin Requests
//
struct TokenIssuance : TokenAdminRequest
{
    using Settings    = BitField<TOKEN_SETTINGS_COUNT>;
    using Controllers = std::vector<ControllerInfo>;

    TokenIssuance() = default;

    TokenIssuance(bool & error,
                  std::basic_streambuf<uint8_t> & stream);

    TokenIssuance(bool & error,
                  boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    std::string symbol;
    std::string name;
    uint16_t    total_supply;
    Settings    settings;
    Controllers controllers;
    std::string issuer_info;
};

struct TokenIssueAdtl : TokenAdminRequest
{
    TokenIssueAdtl(bool & error,
                  std::basic_streambuf<uint8_t> & stream);

    TokenIssueAdtl(bool & error,
                   boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    uint16_t amount;
};

struct TokenChangeSetting : TokenAdminRequest
{
    TokenChangeSetting(bool & error,
                       std::basic_streambuf<uint8_t> & stream);

    TokenChangeSetting(bool & error,
                       boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    TokenSetting setting;
    SettingValue value;
};

struct TokenImmuteSetting : TokenAdminRequest
{
    TokenImmuteSetting(bool & error,
                       std::basic_streambuf<uint8_t> & stream);

    TokenImmuteSetting(bool & error,
                       boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    TokenSetting setting;
};

struct TokenRevoke : TokenAdminRequest
{
    using Transaction = ::Transaction<uint16_t>;

    TokenRevoke(bool & error,
                std::basic_streambuf<uint8_t> & stream);

    TokenRevoke(bool & error,
                boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    AccountAddress source;
    Transaction    transaction;
};

struct TokenFreeze : TokenAdminRequest
{
    TokenFreeze(bool & error,
                std::basic_streambuf<uint8_t> & stream);

    TokenFreeze(bool & error,
                boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    AccountAddress account;
    FreezeAction   action;
};

struct TokenSetFee : TokenAdminRequest
{
    TokenSetFee(bool & error,
                std::basic_streambuf<uint8_t> & stream);

    TokenSetFee(bool & error,
                boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    TokenFeeType fee_type;
    uint16_t     fee_rate;
};

struct TokenWhitelist : TokenAdminRequest
{
    TokenWhitelist(bool & error,
                   std::basic_streambuf<uint8_t> & stream);

    TokenWhitelist(bool & error,
                   boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    AccountAddress account;
};

struct TokenIssuerInfo : TokenAdminRequest
{
    TokenIssuerInfo(bool & error,
                    std::basic_streambuf<uint8_t> & stream);

    TokenIssuerInfo(bool & error,
                    boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    std::string new_info;
};

struct TokenController : TokenAdminRequest
{
    TokenController(bool & error,
                    std::basic_streambuf<uint8_t> & stream);

    TokenController(bool & error,
                    boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    // TODO: controller.privileges is
    //       ignored when action == remove?
    //
    ControllerAction action;
    ControllerInfo   controller;
};

struct TokenBurn : TokenAdminRequest
{
    TokenBurn(bool & error,
              std::basic_streambuf<uint8_t> & stream);

    TokenBurn(bool & error,
              boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    uint16_t amount;
};

struct TokenAccountSend : TokenAdminRequest
{
    using Transaction = ::Transaction<uint16_t>;

    TokenAccountSend(bool & error,
                     std::basic_streambuf<uint8_t> & stream);

    TokenAccountSend(bool & error,
                     boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    Transaction transaction;
};

struct TokenAccountWithdrawFee : TokenAdminRequest
{
    using Transaction = ::Transaction<uint16_t>;

    TokenAccountWithdrawFee(bool & error,
                            std::basic_streambuf<uint8_t> & stream);

    TokenAccountWithdrawFee(bool & error,
                            boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    Transaction transaction;
};

// Token User Requests
//
struct TokenSend : TokenRequest
{
    using Transaction = ::Transaction<uint16_t>;
    using Transactions = std::vector<Transaction>;

    TokenSend(bool & error,
              std::basic_streambuf<uint8_t> & stream);

    TokenSend(bool & error,
              boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    Transactions transactions;
    uint16_t     fee;
};
