#pragma once

#include <logos/request/transaction.hpp>
#include <logos/token/common.hpp>
#include <logos/lib/numbers.hpp>

// Token Admin Requests
//
struct TokenIssuance : TokenRequest
{
    using Request::Hash;

    using Settings    = BitField<TOKEN_SETTINGS_COUNT>;
    using Controllers = std::vector<ControllerInfo>;

    TokenIssuance() = default;

    TokenIssuance(bool & error,
                  const logos::mdb_val & mdbval);

    TokenIssuance(bool & error,
                  std::basic_streambuf<uint8_t> & stream);

    TokenIssuance(bool & error,
                  boost::property_tree::ptree const & tree);

    logos::AccountType GetAccountType() const override;
    AccountAddress GetAccount() const override;
    AccountAddress GetSource() const override;

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    std::string  symbol;
    std::string  name;
    uint16_t     total_supply;
    TokenFeeType fee_type;
    uint16_t     fee_rate;
    Settings     settings;
    Controllers  controllers;
    std::string  issuer_info;
};

struct TokenIssueAdtl : TokenRequest
{
    using Request::Hash;

    TokenIssueAdtl() = default;

    TokenIssueAdtl(bool & error,
                   const logos::mdb_val & mdbval);

    TokenIssueAdtl(bool & error,
                  std::basic_streambuf<uint8_t> & stream);

    TokenIssueAdtl(bool & error,
                   boost::property_tree::ptree const & tree);

    AccountAddress GetSource() const override;

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    uint16_t amount;
};

struct TokenChangeSetting : TokenRequest
{
    using Request::Hash;
    
    TokenChangeSetting() = default;

    TokenChangeSetting(bool & error,
                       const logos::mdb_val & mdbval);

    TokenChangeSetting(bool & error,
                       std::basic_streambuf<uint8_t> & stream);

    TokenChangeSetting(bool & error,
                       boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    TokenSetting setting;
    SettingValue value;
};

struct TokenImmuteSetting : TokenRequest
{
    using Request::Hash;

    TokenImmuteSetting() = default;

    TokenImmuteSetting(bool & error,
                       const logos::mdb_val & mdbval);

    TokenImmuteSetting(bool & error,
                       std::basic_streambuf<uint8_t> & stream);

    TokenImmuteSetting(bool & error,
                       boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    TokenSetting setting;
};

struct TokenRevoke : TokenRequest
{
    using Request::Hash;

    using Transaction = ::Transaction<uint16_t>;

    TokenRevoke() = default;

    TokenRevoke(bool & error,
                const logos::mdb_val & mdbval);

    TokenRevoke(bool & error,
                std::basic_streambuf<uint8_t> & stream);

    TokenRevoke(bool & error,
                boost::property_tree::ptree const & tree);

    AccountAddress GetSource() const override;
    logos::AccountType GetSourceType() const override;
    uint16_t GetTokenTotal() const override;

    bool Validate(logos::process_return & result,
                  std::shared_ptr<logos::Account> info) const override;

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    AccountAddress source;
    Transaction    transaction;
};

struct TokenFreeze : TokenRequest
{
    using Request::Hash;

    TokenFreeze() = default;

    TokenFreeze(bool & error,
                const logos::mdb_val & mdbval);

    TokenFreeze(bool & error,
                std::basic_streambuf<uint8_t> & stream);

    TokenFreeze(bool & error,
                boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    AccountAddress account;
    FreezeAction   action;
};

struct TokenSetFee : TokenRequest
{
    using Request::Hash;

    TokenSetFee() = default;

    TokenSetFee(bool & error,
                const logos::mdb_val & mdbval);

    TokenSetFee(bool & error,
                std::basic_streambuf<uint8_t> & stream);

    TokenSetFee(bool & error,
                boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    TokenFeeType fee_type;
    uint16_t     fee_rate;
};

struct TokenWhitelist : TokenRequest
{
    using Request::Hash;

    TokenWhitelist() = default;

    TokenWhitelist(bool & error,
                   const logos::mdb_val & mdbval);

    TokenWhitelist(bool & error,
                   std::basic_streambuf<uint8_t> & stream);

    TokenWhitelist(bool & error,
                   boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    AccountAddress account;
};

struct TokenIssuerInfo : TokenRequest
{
    using Request::Hash;

    TokenIssuerInfo() = default;

    TokenIssuerInfo(bool & error,
                    const logos::mdb_val & mdbval);

    TokenIssuerInfo(bool & error,
                    std::basic_streambuf<uint8_t> & stream);

    TokenIssuerInfo(bool & error,
                    boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    std::string new_info;
};

struct TokenController : TokenRequest
{
    using Request::Hash;

    TokenController() = default;

    TokenController(bool & error,
                    const logos::mdb_val & mdbval);

    TokenController(bool & error,
                    std::basic_streambuf<uint8_t> & stream);

    TokenController(bool & error,
                    boost::property_tree::ptree const & tree);

    bool Validate(logos::process_return & result) const override;
    bool Validate(logos::process_return & result,
                  std::shared_ptr<logos::Account> info) const override;

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    // TODO: controller.privileges is
    //       ignored when action == remove?
    //
    ControllerAction action;
    ControllerInfo   controller;
};

struct TokenBurn : TokenRequest
{
    using Request::Hash;

    TokenBurn() = default;

    TokenBurn(bool & error,
              const logos::mdb_val & mdbval);

    TokenBurn(bool & error,
              std::basic_streambuf<uint8_t> & stream);

    TokenBurn(bool & error,
              boost::property_tree::ptree const & tree);

    uint16_t GetTokenTotal() const override;
    logos::AccountType GetSourceType() const override;

    bool Validate(logos::process_return & result,
                  std::shared_ptr<logos::Account> info) const override;

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    uint16_t amount;
};

struct TokenAccountSend : TokenRequest
{
    using Request::Hash;

    using Transaction = ::Transaction<uint16_t>;

    TokenAccountSend() = default;

    TokenAccountSend(bool & error,
                     const logos::mdb_val & mdbval);

    TokenAccountSend(bool & error,
                     std::basic_streambuf<uint8_t> & stream);

    TokenAccountSend(bool & error,
                     boost::property_tree::ptree const & tree);

    uint16_t GetTokenTotal() const override;
    logos::AccountType GetSourceType() const override;

    bool Validate(logos::process_return & result,
                  std::shared_ptr<logos::Account> info) const override;

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    Transaction transaction;
};

struct TokenAccountWithdrawFee : TokenRequest
{
    using Request::Hash;

    using Transaction = ::Transaction<uint16_t>;

    TokenAccountWithdrawFee() = default;

    TokenAccountWithdrawFee(bool & error,
                            const logos::mdb_val & mdbval);

    TokenAccountWithdrawFee(bool & error,
                            std::basic_streambuf<uint8_t> & stream);

    TokenAccountWithdrawFee(bool & error,
                            boost::property_tree::ptree const & tree);

    uint16_t GetTokenTotal() const override;
    logos::AccountType GetSourceType() const override;

    bool Validate(logos::process_return & result,
                  std::shared_ptr<logos::Account> info) const override;

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    Transaction transaction;
};

// Token User Requests
//
struct TokenSend : TokenRequest
{
    using Request::Hash;

    using Transaction  = ::Transaction<uint16_t>;
    using Transactions = std::vector<Transaction>;

    TokenSend() = default;

    TokenSend(bool & error,
              const logos::mdb_val & mdbval);

    TokenSend(bool & error,
              std::basic_streambuf<uint8_t> & stream);

    TokenSend(bool & error,
              boost::property_tree::ptree const & tree);

    uint16_t GetTokenTotal() const override;
    logos::AccountType GetSourceType() const override;

    bool Validate(logos::process_return & result,
                  std::shared_ptr<logos::Account> info) const override;

    logos::AccountType GetAccountType() const override;
    AccountAddress GetAccount() const override;

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    Transactions transactions;
    uint16_t     token_fee;
};
