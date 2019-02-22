#pragma once

#include <logos/token/common.hpp>
#include <logos/lib/numbers.hpp>

// Token Admin Requests
//
struct TokenIssuance : TokenRequest
{
    using Request::Hash;

    using Settings    = BitField<TOKEN_SETTINGS_COUNT>;
    using Controllers = std::vector<ControllerInfo>;

    TokenIssuance();

    TokenIssuance(bool & error,
                  const logos::mdb_val & mdbval);

    TokenIssuance(bool & error,
                  std::basic_streambuf<uint8_t> & stream);

    TokenIssuance(bool & error,
                  boost::property_tree::ptree const & tree);

    bool Validate(logos::process_return & result) const override;

    logos::AccountType GetAccountType() const override;
    AccountAddress GetAccount() const override;
    AccountAddress GetSource() const override;

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    bool operator==(const Request & other) const override;

    static constexpr uint8_t  SYMBOL_MAX_SIZE = 8;
    static constexpr uint8_t  NAME_MAX_SIZE   = 32;
    static constexpr uint16_t INFO_MAX_SIZE   = 512;

    std::string  symbol;
    std::string  name;
    Amount       total_supply;
    TokenFeeType fee_type;
    Amount       fee_rate;
    Settings     settings;
    Controllers  controllers;
    std::string  issuer_info;
};

struct TokenIssueAdtl : TokenRequest
{
    using Request::Hash;

    TokenIssueAdtl();

    TokenIssueAdtl(bool & error,
                   const logos::mdb_val & mdbval);

    TokenIssueAdtl(bool & error,
                  std::basic_streambuf<uint8_t> & stream);

    TokenIssueAdtl(bool & error,
                   boost::property_tree::ptree const & tree);

    bool Validate(logos::process_return & result,
                  std::shared_ptr<logos::Account> info) const override;

    AccountAddress GetSource() const override;

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    bool operator==(const Request & other) const override;

    Amount amount;
};

struct TokenChangeSetting : TokenRequest
{
    using Request::Hash;
    
    TokenChangeSetting();

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

    bool operator==(const Request & other) const override;

    TokenSetting setting;
    SettingValue value;
};

struct TokenImmuteSetting : TokenRequest
{
    using Request::Hash;

    TokenImmuteSetting();

    TokenImmuteSetting(bool & error,
                       const logos::mdb_val & mdbval);

    TokenImmuteSetting(bool & error,
                       std::basic_streambuf<uint8_t> & stream);

    TokenImmuteSetting(bool & error,
                       boost::property_tree::ptree const & tree);

    bool Validate(logos::process_return & result) const override;

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    bool operator==(const Request & other) const override;

    TokenSetting setting;
};

struct TokenRevoke : TokenRequest
{
    using Request::Hash;

    TokenRevoke();

    TokenRevoke(bool & error,
                const logos::mdb_val & mdbval);

    TokenRevoke(bool & error,
                std::basic_streambuf<uint8_t> & stream);

    TokenRevoke(bool & error,
                boost::property_tree::ptree const & tree);

    AccountAddress GetSource() const override;
    logos::AccountType GetSourceType() const override;
    Amount GetTokenTotal() const override;

    bool Validate(logos::process_return & result,
                  std::shared_ptr<logos::Account> info) const override;

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    bool operator==(const Request & other) const override;

    AccountAddress source;
    Transaction    transaction;
};

struct TokenFreeze : TokenRequest
{
    using Request::Hash;

    TokenFreeze();

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

    bool operator==(const Request & other) const override;

    AccountAddress account;
    FreezeAction   action;
};

struct TokenSetFee : TokenRequest
{
    using Request::Hash;

    TokenSetFee();

    TokenSetFee(bool & error,
                const logos::mdb_val & mdbval);

    TokenSetFee(bool & error,
                std::basic_streambuf<uint8_t> & stream);

    TokenSetFee(bool & error,
                boost::property_tree::ptree const & tree);

    bool Validate(logos::process_return & result) const override;

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    bool operator==(const Request & other) const override;

    TokenFeeType fee_type;
    Amount       fee_rate;
};

struct TokenWhitelist : TokenRequest
{
    using Request::Hash;

    TokenWhitelist();

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

    bool operator==(const Request & other) const override;

    AccountAddress account;
};

struct TokenIssuerInfo : TokenRequest
{
    using Request::Hash;

    TokenIssuerInfo();

    TokenIssuerInfo(bool & error,
                    const logos::mdb_val & mdbval);

    TokenIssuerInfo(bool & error,
                    std::basic_streambuf<uint8_t> & stream);

    TokenIssuerInfo(bool & error,
                    boost::property_tree::ptree const & tree);

    bool Validate(logos::process_return & result) const override;

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    bool operator==(const Request & other) const override;

    std::string new_info;
};

struct TokenController : TokenRequest
{
    using Request::Hash;

    TokenController();

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

    bool operator==(const Request & other) const override;

    // TODO: controller.privileges is
    //       ignored when action == remove?
    //
    ControllerAction action;
    ControllerInfo   controller;
};

struct TokenBurn : TokenRequest
{
    using Request::Hash;

    TokenBurn();

    TokenBurn(bool & error,
              const logos::mdb_val & mdbval);

    TokenBurn(bool & error,
              std::basic_streambuf<uint8_t> & stream);

    TokenBurn(bool & error,
              boost::property_tree::ptree const & tree);

    Amount GetTokenTotal() const override;
    logos::AccountType GetSourceType() const override;

    bool Validate(logos::process_return & result,
                  std::shared_ptr<logos::Account> info) const override;

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    bool operator==(const Request & other) const override;

    Amount amount;
};

struct TokenAccountSend : TokenRequest
{
    using Request::Hash;

    TokenAccountSend();

    TokenAccountSend(bool & error,
                     const logos::mdb_val & mdbval);

    TokenAccountSend(bool & error,
                     std::basic_streambuf<uint8_t> & stream);

    TokenAccountSend(bool & error,
                     boost::property_tree::ptree const & tree);

    Amount GetTokenTotal() const override;
    logos::AccountType GetSourceType() const override;

    bool Validate(logos::process_return & result,
                  std::shared_ptr<logos::Account> info) const override;

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    bool operator==(const Request & other) const override;

    Transaction transaction;
};

struct TokenAccountWithdrawFee : TokenRequest
{
    using Request::Hash;

    TokenAccountWithdrawFee();

    TokenAccountWithdrawFee(bool & error,
                            const logos::mdb_val & mdbval);

    TokenAccountWithdrawFee(bool & error,
                            std::basic_streambuf<uint8_t> & stream);

    TokenAccountWithdrawFee(bool & error,
                            boost::property_tree::ptree const & tree);

    Amount GetTokenTotal() const override;
    logos::AccountType GetSourceType() const override;

    bool Validate(logos::process_return & result,
                  std::shared_ptr<logos::Account> info) const override;

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    bool operator==(const Request & other) const override;

    Transaction transaction;
};

// Token User Requests
//
struct TokenSend : TokenRequest
{
    using Request::Hash;

    using Transactions = std::vector<Transaction>;

    TokenSend();

    TokenSend(bool & error,
              const logos::mdb_val & mdbval);

    TokenSend(bool & error,
              std::basic_streambuf<uint8_t> & stream);

    TokenSend(bool & error,
              boost::property_tree::ptree const & tree);

    Amount GetTokenTotal() const override;
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

    bool operator==(const Request & other) const override;

    Transactions transactions;
    Amount       token_fee;
};
