#pragma once

#include <logos/consensus/messages/byte_arrays.hpp>
#include <logos/common.hpp>

#include <stdint.h>
#include <bitset>

// XXX - Ensure that settings with odd values
//       represent the mutability of the previous
//       setting.
enum class TokenSetting : uint8_t
{
    AddTokens          = 0,
    ModifyAddTokens    = 1,
    Revoke             = 2,
    ModifyRevoke       = 3,
    Freeze             = 4,
    ModifyFreeze       = 5,
    AdjustFee          = 6,
    ModifyAdjustFee    = 7,
    Whitelisting       = 8,
    ModifyWhitelisting = 9
};

enum class SettingValue : uint8_t
{
    Enabled  = 0,
    Disabled = 1
};

enum class TokenFeeType : uint8_t
{
    Percentage = 0,
    Flat       = 1
};

enum class ControllerAction : uint8_t
{
    Add    = 0,
    Remove = 1
};

enum class FreezeAction : uint8_t
{
    Freeze   = 0,
    Unfreeze = 1
};

enum class ControllerPrivilege : uint8_t
{
    // Privileges for modifying
    // token account settings.
    ChangeAddTokens          = 0,
    ChangeModifyAddTokens    = 1,
    ChangeRevoke             = 2,
    ChangeModifyRevoke       = 3,
    ChangeFreeze             = 4,
    ChangeModifyFreeze       = 5,
    ChangeAdjustFee          = 6,
    ChangeModifyAdjustFee    = 7,
    ChangeWhitelisting       = 8,
    ChangeModifyWhitelisting = 9,

    // Privileges for performing
    // token-related actions in
    // the system.
    PromoteController        = 10,
    AddTokens                = 11,
    Revoke                   = 12,
    Freeze                   = 13,
    AdjestFee                = 14,
    Whitelist                = 15,
    Burn                     = 16,
    Withdraw                 = 17,
    WithdrawFee              = 18
};

const size_t TOKEN_SETTINGS_COUNT       = 10;
const size_t CONTROLLER_PRIVILEGE_COUNT = 19;

struct TokenRequest : logos::Request
{
    TokenRequest(bool & error,
                 std::basic_streambuf<uint8_t> & stream);

   void Hash(blake2b_state & hash) const override;

   uint16_t WireSize() const override;

   logos::block_hash token_id;
};

struct TokenAdminRequest : TokenRequest
{
    TokenAdminRequest(bool & error,
                      std::basic_streambuf<uint8_t> & stream);

    void Hash(blake2b_state & hash) const override;

    uint16_t WireSize() const override;

    AccountAddress admin_address;
};

struct ControllerInfo
{
    using Privelages = std::bitset<CONTROLLER_PRIVILEGE_COUNT>;

    ControllerInfo(bool & error,
                   std::basic_streambuf<uint8_t> & stream);

    AccountAddress address;
    Privelages     privileges;
};

struct TokenTransaction
{
    TokenTransaction(bool & error,
                     std::basic_streambuf<uint8_t> & stream);

    AccountAddress dest;
    uint16_t       amount;
};
