#include <logos/token/common.hpp>

#include <logos/token/requests.hpp>
#include <logos/request/fields.hpp>
#include <logos/token/account.hpp>
#include <logos/token/utility.hpp>

TokenRequest::TokenRequest(RequestType type)
    : Request(type)
{}

TokenRequest::TokenRequest(bool & error,
                           std::basic_streambuf<uint8_t> & stream)
    : Request(error, stream)
{
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

TokenRequest::TokenRequest(bool & error,
                           boost::property_tree::ptree const & tree)
    : Request(error, tree)
{
    using namespace request::fields;

    if(error)
    {
        return;
    }

    try
    {
        error = token_id.decode_hex(tree.get<std::string>(TOKEN_ID));
    }
    catch(...)
    {
        error = true;
    }
}

bool TokenRequest::Validate(logos::process_return & result) const
{
    if(!Request::Validate(result))
    {
        return false;
    }

    if(token_id.is_zero())
    {
        result.code = logos::process_result::invalid_token_id;
        return false;
    }

    return true;
}

bool TokenRequest::ValidateFee(TokenFeeType fee_type, Amount fee_rate) const
{
    auto result = false;

    switch(fee_type)
    {
        case TokenFeeType::Percentage:
            if(fee_rate.number() <= 100)
            {
                result = true;
            }
            break;
        case TokenFeeType::Flat:
            result = ValidateTokenAmount(fee_rate, false);
            break;
        case TokenFeeType::Unknown:
            break;
    }

    return result;
}

bool TokenRequest::ValidateTokenAmount(const Amount & amount, bool non_zero) const
{
    if(non_zero and amount.is_zero())
    {
        return false;
    }

    return amount.number() % TOKEN_RAW == 0;
}

logos::AccountType TokenRequest::GetAccountType() const
{
    return logos::AccountType::TokenAccount;
}

logos::AccountType TokenRequest::GetSourceType() const
{
    return logos::AccountType::TokenAccount;
}

AccountAddress TokenRequest::GetAccount() const
{
    return token_id;
}

AccountAddress TokenRequest::GetSource() const
{
    // Source and Account are the same
    // for most TokenRequests.
    return GetAccount();
}

AccountAddress TokenRequest::GetDestination() const
{
    return {0};
}

boost::property_tree::ptree TokenRequest::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = Request::SerializeJson();

    tree.put(TOKEN_ID, token_id.to_string());

    return tree;
}

uint64_t TokenRequest::Serialize(logos::stream & stream) const
{
    return logos::write(stream, token_id);
}

void TokenRequest::Deserialize(bool & error, logos::stream & stream)
{
    error = read(stream, token_id);
}

void TokenRequest::DeserializeDB(bool & error, logos::stream & stream)
{
    Request::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

void TokenRequest::Hash(blake2b_state & hash) const
{
    Request::Hash(hash);
    token_id.Hash(hash);
}

uint16_t TokenRequest::WireSize() const
{
    return sizeof(token_id.bytes) + Request::WireSize();
}

ControllerInfo::ControllerInfo(const AccountAddress & account,
                               const Privileges & privileges)
    : account(account)
    , privileges(privileges)
{}

ControllerInfo::ControllerInfo(bool & error,
                               std::basic_streambuf<uint8_t> & stream)
{
    error = logos::read(stream, account);
    if(error)
    {
        return;
    }

    privileges = Privileges(error, stream);
}

ControllerInfo::ControllerInfo(bool & error,
                               boost::property_tree::ptree const & tree)
{
    DeserializeJson(error, tree);
}

void ControllerInfo::DeserializeJson(bool & error,
                                     boost::property_tree::ptree const & tree)
{
    using namespace request::fields;

    try
    {
        error = account.decode_account(tree.get<std::string>(ACCOUNT));
        if(error)
        {
            return;
        }

        auto privileges_tree = tree.get_child(PRIVILEGES);
        privileges.DeserializeJson(error, privileges_tree,
                                   [](bool & error, const std::string & data)
                                   {
                                       return size_t(GetControllerPrivilege(error, data));
                                   });
    }
    catch(...)
    {
        error = true;
    }
}

boost::property_tree::ptree ControllerInfo::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree;

    tree.put(ACCOUNT, account.to_account());

    boost::property_tree::ptree privileges_tree(
        privileges.SerializeJson([](size_t pos)
                                 {
                                     return GetControllerPrivilegeField(pos);
                                 }));

    tree.add_child(PRIVILEGES, privileges_tree);

    return tree;
}

uint64_t ControllerInfo::Serialize(logos::stream & stream) const
{
    return logos::write(stream, account) +
           privileges.Serialize(stream);
}

void ControllerInfo::Hash(blake2b_state & hash) const
{
    account.Hash(hash);
    privileges.Hash(hash);
}

uint16_t ControllerInfo::WireSize()
{
    return sizeof(account.bytes) + Privileges::WireSize();
}

bool ControllerInfo::IsAuthorized(std::shared_ptr<const Request> request) const
{
    bool result = false;

    switch(request->type)
    {
        // TODO: N/A
        case RequestType::Send:
        case RequestType::Change:
        case RequestType::Issuance:
            break;
        case RequestType::IssueAdditional:
            result = privileges[size_t(ControllerPrivilege::Issuance)];
            break;
        case RequestType::ChangeSetting:
        {
            auto change = static_pointer_cast<const ChangeSetting>(request);
            result = IsAuthorized(change->setting);
            break;
        }
        case RequestType::ImmuteSetting:
        {
            auto immute = static_pointer_cast<const ImmuteSetting>(request);
            result = IsAuthorized(TokenAccount::GetMutabilitySetting(immute->setting));
            break;
        }
        case RequestType::Revoke:
            result = privileges[size_t(ControllerPrivilege::Revoke)];
            break;
        case RequestType::AdjustUserStatus:
        {
            auto adjust = static_pointer_cast<const AdjustUserStatus>(request);
            result = IsAuthorized(adjust->status);
            break;
        }
        case RequestType::AdjustFee:
            result = privileges[size_t(ControllerPrivilege::AdjustFee)];
            break;
        case RequestType::UpdateIssuerInfo:
            result = privileges[size_t(ControllerPrivilege::UpdateIssuerInfo)];
            break;
        case RequestType::UpdateController:
            result = privileges[size_t(ControllerPrivilege::UpdateController)];
            break;
        case RequestType::Burn:
            result = privileges[size_t(ControllerPrivilege::Burn)];
            break;
        case RequestType::Distribute:
            result = privileges[size_t(ControllerPrivilege::Distribute)];
            break;
        case RequestType::WithdrawFee:
            result = privileges[size_t(ControllerPrivilege::WithdrawFee)];
            break;
        case RequestType::WithdrawLogos:
            result = privileges[size_t(ControllerPrivilege::WithdrawLogos)];
            break;

        // TODO: N/A
        case RequestType::TokenSend:
        case RequestType::Unknown:
            result = false;
            break;
    }

    return result;
}

bool ControllerInfo::IsAuthorized(UserStatus status) const
{
    bool result = false;

    switch(status)
    {
        case UserStatus::Frozen:
        case UserStatus::Unfrozen:
            result = privileges[size_t(ControllerPrivilege::Freeze)];
            break;
        case UserStatus::Whitelisted:
        case UserStatus::NotWhitelisted:
            result = privileges[size_t(ControllerPrivilege::Whitelist)];
            break;
        default:
            break;
    }

    return result;
}

bool ControllerInfo::IsAuthorized(TokenSetting setting) const
{
    bool result;

    switch(setting)
    {
        case TokenSetting::Issuance:
            result = privileges[size_t(ControllerPrivilege::ChangeIssuance)];
            break;
        case TokenSetting::ModifyIssuance:
            result = privileges[size_t(ControllerPrivilege::ChangeModifyIssuance)];
            break;
        case TokenSetting::Revoke:
            result = privileges[size_t(ControllerPrivilege::ChangeRevoke)];
            break;
        case TokenSetting::ModifyRevoke:
            result = privileges[size_t(ControllerPrivilege::ChangeModifyRevoke)];
            break;
        case TokenSetting::Freeze:
            result = privileges[size_t(ControllerPrivilege::ChangeFreeze)];
            break;
        case TokenSetting::ModifyFreeze:
            result = privileges[size_t(ControllerPrivilege::ChangeModifyFreeze)];
            break;
        case TokenSetting::AdjustFee:
            result = privileges[size_t(ControllerPrivilege::ChangeAdjustFee)];
            break;
        case TokenSetting::ModifyAdjustFee:
            result = privileges[size_t(ControllerPrivilege::ChangeModifyAdjustFee)];
            break;
        case TokenSetting::Whitelist:
            result = privileges[size_t(ControllerPrivilege::ChangeWhitelist)];
            break;
        case TokenSetting::ModifyWhitelist:
            result = privileges[size_t(ControllerPrivilege::ChangeModifyWhitelist)];
            break;
        case TokenSetting::Unknown:
            result = false;
            break;
    }

    return result;
}

bool ControllerInfo::operator== (const ControllerInfo & other) const
{
    return account == other.account &&
           privileges == other.privileges;
}

