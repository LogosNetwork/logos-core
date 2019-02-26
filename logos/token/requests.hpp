#pragma once

#include <logos/token/common.hpp>
#include <logos/lib/numbers.hpp>

struct Issuance : TokenRequest
{
    using Request::Hash;

    using Settings    = BitField<TOKEN_SETTINGS_COUNT>;
    using Controllers = std::vector<ControllerInfo>;

    Issuance();

    Issuance(bool & error,
             const logos::mdb_val & mdbval);

    Issuance(bool & error,
             std::basic_streambuf<uint8_t> & stream);

    Issuance(bool & error,
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

struct IssueAdditional : TokenRequest
{
    using Request::Hash;

    IssueAdditional();

    IssueAdditional(bool & error,
                    const logos::mdb_val & mdbval);

    IssueAdditional(bool & error,
                    std::basic_streambuf<uint8_t> & stream);

    IssueAdditional(bool & error,
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

struct ChangeSetting : TokenRequest
{
    using Request::Hash;

    ChangeSetting();

    ChangeSetting(bool & error,
                  const logos::mdb_val & mdbval);

    ChangeSetting(bool & error,
                  std::basic_streambuf<uint8_t> & stream);

    ChangeSetting(bool & error,
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

struct ImmuteSetting : TokenRequest
{
    using Request::Hash;

    ImmuteSetting();

    ImmuteSetting(bool & error,
                  const logos::mdb_val & mdbval);

    ImmuteSetting(bool & error,
                  std::basic_streambuf<uint8_t> & stream);

    ImmuteSetting(bool & error,
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

struct Revoke : TokenRequest
{
    using Request::Hash;

    Revoke();

    Revoke(bool & error,
           const logos::mdb_val & mdbval);

    Revoke(bool & error,
           std::basic_streambuf<uint8_t> & stream);

    Revoke(bool & error,
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

struct AdjustUserStatus : TokenRequest
{
    using Request::Hash;

    AdjustUserStatus();

    AdjustUserStatus(bool & error,
                     const logos::mdb_val & mdbval);

    AdjustUserStatus(bool & error,
                     std::basic_streambuf<uint8_t> & stream);

    AdjustUserStatus(bool & error,
                     boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    bool operator==(const Request & other) const override;

    AccountAddress account;
    UserStatus     status;
};

struct AdjustFee : TokenRequest
{
    using Request::Hash;

    AdjustFee();

    AdjustFee(bool & error,
              const logos::mdb_val & mdbval);

    AdjustFee(bool & error,
              std::basic_streambuf<uint8_t> & stream);

    AdjustFee(bool & error,
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

struct UpdateIssuerInfo : TokenRequest
{
    using Request::Hash;

    UpdateIssuerInfo();

    UpdateIssuerInfo(bool & error,
                     const logos::mdb_val & mdbval);

    UpdateIssuerInfo(bool & error,
                     std::basic_streambuf<uint8_t> & stream);

    UpdateIssuerInfo(bool & error,
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

struct UpdateController : TokenRequest
{
    using Request::Hash;

    UpdateController();

    UpdateController(bool & error,
                     const logos::mdb_val & mdbval);

    UpdateController(bool & error,
                     std::basic_streambuf<uint8_t> & stream);

    UpdateController(bool & error,
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

    ControllerAction action;
    ControllerInfo   controller;
};

struct Burn : TokenRequest
{
    using Request::Hash;

    Burn();

    Burn(bool & error,
         const logos::mdb_val & mdbval);

    Burn(bool & error,
         std::basic_streambuf<uint8_t> & stream);

    Burn(bool & error,
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

struct Distribute : TokenRequest
{
    using Request::Hash;

    Distribute();

    Distribute(bool & error,
               const logos::mdb_val & mdbval);

    Distribute(bool & error,
               std::basic_streambuf<uint8_t> & stream);

    Distribute(bool & error,
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

struct WithdrawFee : TokenRequest
{
    using Request::Hash;

    WithdrawFee();

    WithdrawFee(bool & error,
                const logos::mdb_val & mdbval);

    WithdrawFee(bool & error,
                std::basic_streambuf<uint8_t> & stream);

    WithdrawFee(bool & error,
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
