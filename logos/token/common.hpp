#pragma once

#include <logos/consensus/messages/byte_arrays.hpp>
#include <logos/request/transaction.hpp>
#include <logos/request/requests.hpp>
#include <logos/lib/utility.hpp>
#include <logos/common.hpp>

// XXX - Ensure that settings with odd values
//       represent the mutability of the previous
//       setting.
enum class TokenSetting : uint8_t
{
    Issuance        = 0,
    ModifyIssuance  = 1,
    Revoke          = 2,
    ModifyRevoke    = 3,
    Freeze          = 4,
    ModifyFreeze    = 5,
    AdjustFee       = 6,
    ModifyAdjustFee = 7,
    Whitelist       = 8,
    ModifyWhitelist = 9,

    Unknown         = 10
};

enum class SettingValue : uint8_t
{
    Disabled = 0,
    Enabled  = 1
};

enum class PrivilegeValue : uint8_t
{
    Disabled = 0,
    Enabled  = 1
};

enum class TokenFeeType : uint8_t
{
    Percentage = 0,
    Flat       = 1,
    Unknown    = 2
};

enum class ControllerAction : uint8_t
{
    Add     = 0,
    Remove  = 1,
    Unknown = 2
};

enum class UserStatus : uint8_t
{
    Frozen         = 0,
    Unfrozen       = 1,
    Whitelisted    = 2,
    NotWhitelisted = 3,
    Unknown        = 4
};

enum class ControllerPrivilege : uint8_t
{
    // Privileges for modifying
    // token account settings.
    ChangeIssuance        = 0,
    ChangeModifyIssuance  = 1,
    ChangeRevoke          = 2,
    ChangeModifyRevoke    = 3,
    ChangeFreeze          = 4,
    ChangeModifyFreeze    = 5,
    ChangeAdjustFee       = 6,
    ChangeModifyAdjustFee = 7,
    ChangeWhitelist       = 8,
    ChangeModifyWhitelist = 9,

    // Privileges for performing
    // token-related actions in
    // the system.
    Issuance              = 10,
    Revoke                = 11,
    Freeze                = 12,
    AdjustFee             = 13,
    Whitelist             = 14,
    UpdateIssuerInfo      = 15,
    UpdateController      = 16,
    Burn                  = 17,
    Distribute            = 18,
    WithdrawFee           = 19,
    WithdrawLogos         = 20,

    Unknown               = 21
};

// Larger than necessary in anticipation of
// values added for additional capabilities
// in the future.
const size_t TOKEN_SETTINGS_COUNT       = 32;
const size_t CONTROLLER_PRIVILEGE_COUNT = 32;

struct TokenRequest : Request
{
    using InfoSizeT   = uint16_t;
    using Transaction = ::Transaction<Amount>;

    TokenRequest() = default;

    TokenRequest(RequestType type);

    TokenRequest(bool & error,
                 std::basic_streambuf<uint8_t> & stream);

    TokenRequest(bool & error,
                 boost::property_tree::ptree const & tree);

    bool Validate(logos::process_return & result) const override;
    bool ValidateFee(TokenFeeType fee_type, Amount fee_rate) const;
    bool ValidateTokenAmount(const Amount & amount, bool non_zero = true) const;

    logos::AccountType GetAccountType() const override;
    logos::AccountType GetSourceType() const override;

    AccountAddress GetAccount() const override;
    AccountAddress GetSource() const override;

    virtual AccountAddress GetDestination() const;

    boost::property_tree::ptree SerializeJson() const override;
    uint64_t Serialize(logos::stream & stream) const override;
    void Deserialize(bool & error, logos::stream & stream);
    void DeserializeDB(bool & error, logos::stream & stream) override;

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    BlockHash token_id;
};

struct AdjustUserStatus;

struct ControllerInfo
{
    using Privileges = BitField<CONTROLLER_PRIVILEGE_COUNT>;

    ControllerInfo() = default;

    ControllerInfo(const AccountAddress & account,
                   const Privileges & privileges);

    ControllerInfo(bool & error,
                   std::basic_streambuf<uint8_t> & stream);

    ControllerInfo(bool & error,
                   boost::property_tree::ptree const & tree);

    void DeserializeJson(bool & error,
                         boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const;
    uint64_t Serialize(logos::stream & stream) const;

    void Hash(blake2b_state & hash) const;

    static uint16_t WireSize();

    bool IsAuthorized(std::shared_ptr<const Request> request) const;
    bool IsAuthorized(UserStatus status) const;
    bool IsAuthorized(TokenSetting setting) const;

    bool operator==(const ControllerInfo & other) const;

    AccountAddress account;
    Privileges     privileges;
};
