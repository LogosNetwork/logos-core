#include <logos/token/common.hpp>

#include <logos/token/requests.hpp>
#include <logos/request/fields.hpp>
#include <logos/token/util.hpp>

TokenRequest::TokenRequest(bool & error,
                           std::basic_streambuf<uint8_t> & stream)
    : Request(error, stream)
{
    if(error)
    {
        return;
    }

    error = read(stream, token_id);
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

logos::AccountType TokenRequest::GetAccountType() const
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

boost::property_tree::ptree TokenRequest::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = Request::SerializeJson();

    tree.put(TOKEN_ID, token_id.to_string());

    return tree;
}

uint64_t TokenRequest::Serialize(logos::stream & stream) const
{
    return Request::Serialize(stream) +
           logos::write(stream, token_id);
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

ControllerInfo::ControllerInfo(bool & error,
                               std::basic_streambuf<uint8_t> & stream)
   : privileges(error, stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, account);
    if(error)
    {
        return;
    }
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
        case RequestType::ChangeRep:
        case RequestType::IssueTokens:
            break;
        case RequestType::IssueAdtlTokens:
            result = privileges[size_t(ControllerPrivilege::AddTokens)];
            break;
        case RequestType::ImmuteTokenSetting:
            result = IsAuthorized(static_pointer_cast<const TokenImmuteSetting>(request));
            break;
        case RequestType::RevokeTokens:
            result = privileges[size_t(ControllerPrivilege::Revoke)];
            break;
        case RequestType::FreezeTokens:
            result = privileges[size_t(ControllerPrivilege::Freeze)];
            break;
        case RequestType::SetTokenFee:
            result = privileges[size_t(ControllerPrivilege::AdjustFee)];
            break;
        case RequestType::UpdateWhitelist:
            result = privileges[size_t(ControllerPrivilege::Whitelist)];
            break;
        case RequestType::UpdateIssuerInfo:
            result = privileges[size_t(ControllerPrivilege::UpdateIssuerInfo)];
            break;
        case RequestType::UpdateController:
            result = privileges[size_t(ControllerPrivilege::PromoteController)];
            break;
        case RequestType::BurnTokens:
            result = privileges[size_t(ControllerPrivilege::Burn)];
            break;
        case RequestType::DistributeTokens:
            result = privileges[size_t(ControllerPrivilege::Withdraw)];
            break;
        case RequestType::WithdrawTokens:
            result = privileges[size_t(ControllerPrivilege::WithdrawFee)];
            break;

        // TODO: N/A
        case RequestType::SendTokens:
        case RequestType::Unknown:
            result = false;
            break;
    }

    return result;
}

bool ControllerInfo::IsAuthorized(std::shared_ptr<const TokenImmuteSetting> immute) const
{
    bool result;

    switch(immute->setting)
    {
        case TokenSetting::AddTokens:
            result = privileges[size_t(ControllerPrivilege::ChangeAddTokens)];
            break;
        case TokenSetting::ModifyAddTokens:
            result = privileges[size_t(ControllerPrivilege::ChangeModifyAddTokens)];
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

TokenTransaction::TokenTransaction(bool & error,
                                   std::basic_streambuf<uint8_t> & stream)
{
    error = logos::read(stream, destination);
    if(error)
    {
        return;
    }

    error = logos::read(stream, amount);
}

TokenTransaction::TokenTransaction(bool & error,
                                   boost::property_tree::ptree const & tree)
{
    using namespace request::fields;

    try
    {
        error = destination.decode_account(tree.get<std::string>(DESTINATION));
        if(error)
        {
            return;
        }

        amount = std::stoul(tree.get<std::string>(AMOUNT));
    }
    catch(...)
    {
        error = true;
    }
}

boost::property_tree::ptree TokenTransaction::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree;

    tree.put(DESTINATION, destination.to_account());
    tree.put(AMOUNT, amount);

    return tree;
}

uint64_t TokenTransaction::Serialize(logos::stream & stream) const
{
    return logos::write(stream, destination) +
           logos::write(stream, amount);
}

void TokenTransaction::Hash(blake2b_state & hash) const
{
    destination.Hash(hash);
    blake2b_update(&hash, &amount, sizeof(amount));
}

uint16_t TokenTransaction::WireSize()
{
    return sizeof(destination.bytes) +
           sizeof(amount);
}
