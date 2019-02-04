#include <logos/token/account.hpp>

#include <logos/token/requests.hpp>

#include <ios>

TokenAccount::TokenAccount(bool & error, const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()), mdbval.size());
    error = Deserialize(stream);
}

TokenAccount::TokenAccount(const logos::block_hash & head,
                           logos::amount balance,
                           uint16_t token_balance,
                           uint16_t token_fee_balance,
                           uint32_t block_count)
    : logos::Account(head, balance, block_count)
    , token_balance(token_balance)
    , token_fee_balance(token_fee_balance)
{}

uint32_t TokenAccount::Serialize(logos::stream & stream) const
{
    assert(controllers.size() < MAX_CONTROLLERS);

    auto s = Account::Serialize(stream);
    s += logos::write(stream, token_balance);
    s += logos::write(stream, token_fee_balance);

    s += logos::write(stream, uint8_t(controllers.size()));
    for(auto & c : controllers)
    {
        s += c.Serialize(stream);
    }

    s += logos::write(stream, settings);

    return s;
}

bool TokenAccount::Deserialize(logos::stream & stream)
{
    auto error = Account::Deserialize(stream);
    if(error)
    {
        return error;
    }

    error = logos::read(stream, token_balance);
    if(error)
    {
        return error;
    }

    error = logos::read(stream, token_fee_balance);
    if(error)
    {
        return error;
    }

    uint8_t size;
    error = logos::read(stream, size);
    if(error)
    {
        return error;
    }

    assert(size < MAX_CONTROLLERS);
    for(uint8_t i = 0; i < size; ++i)
    {
        ControllerInfo c(error, stream);
        if(error)
        {
            return error;
        }

        controllers.push_back(c);
    }

    return (error = logos::read(stream, settings));
}

bool TokenAccount::operator== (TokenAccount const & other) const
{
    return token_balance == other.token_balance &&
           token_fee_balance == other.token_fee_balance &&
           Account::operator==(other);
}

bool TokenAccount::operator!= (TokenAccount const & other_a) const
{
    return !(*this == other_a);
}

logos::mdb_val TokenAccount::to_mdb_val(std::vector<uint8_t> &buf) const
{
    assert(buf.empty());
    {
        logos::vectorstream stream(buf);
        Serialize(stream);
    }
    return logos::mdb_val(buf.size(), buf.data());
}

bool TokenAccount::Validate(TokenSetting setting, bool value, logos::process_return & result) const
{
    auto pos = static_cast<EnumType>(setting);
    bool cur_val = settings.test(pos);

    // Settings with odd values represent the
    // mutability of the previous setting.
    if(IsMutabilitySetting(setting))
    {
        // Mutability is false.
        if(!cur_val)
        {
            LOG_ERROR(log) << "Attempt to update a false mutability setting: "
                            << TokenSettingName(setting);

            result.code = logos::process_result::revert_immutability;
            return false;
        }
    }
    else
    {
        auto ms = static_cast<EnumType>(GetMutabilitySetting(setting));

        if(!settings.test(ms))
        {
            LOG_ERROR(log) << "Attempt to update immutable setting: "
                            << TokenSettingName(setting);

            result.code = logos::process_result::immutable;
            return false;;
        }
    }

    if(cur_val == value)
    {
        LOG_WARN(log) << "Redundantly setting ("
                       << TokenSettingName(setting)
                       << ") to "
                       << std::boolalpha << value;

        result.code = logos::process_result::redundant;
    }

    result.code = logos::process_result::progress;
    return true;
}

bool TokenAccount::SendAllowed(const TokenUserStatus & status,
                               logos::process_return & result) const
{
    bool whitelisting = settings[size_t(TokenSetting::Whitelist)];
    bool freezing = settings[size_t(TokenSetting::Freeze)];

    if(whitelisting)
    {
        if(!status.whitelisted)
        {
            result.code = logos::process_result::not_whitelisted;
            return false;
        }
    }

    if(freezing)
    {
        if(status.frozen)
        {
            result.code = logos::process_result::frozen;
            return false;
        }
    }

    return true;
}

bool TokenAccount::IsAllowed(std::shared_ptr<const Request> request) const
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
            result = settings[size_t(TokenSetting::AddTokens)];
            break;
        case RequestType::ImmuteTokenSetting:
            result = IsAllowed(static_pointer_cast<const TokenImmuteSetting>(request));
            break;
        case RequestType::RevokeTokens:
            result = settings[size_t(TokenSetting::Revoke)];
            break;
        case RequestType::FreezeTokens:
            result = settings[size_t(TokenSetting::Freeze)];
            break;
        case RequestType::SetTokenFee:
            result = settings[size_t(TokenSetting::AdjustFee)];
            break;
        case RequestType::UpdateWhitelist:
            result = settings[size_t(TokenSetting::Whitelist)];
            break;
        case RequestType::UpdateIssuerInfo:
        case RequestType::UpdateController:
        case RequestType::BurnTokens:
        case RequestType::DistributeTokens:
        case RequestType::WithdrawTokens:
        case RequestType::SendTokens:
            result = true;
            break;
        case RequestType::Unknown:
            result = false;
            break;
    }

    return result;
}

bool TokenAccount::IsAllowed(std::shared_ptr<const TokenImmuteSetting> immute) const
{
    bool result = false;

    switch(immute->setting)
    {
        case TokenSetting::AddTokens:
            result = settings[size_t(TokenSetting::ModifyAddTokens)];
            break;

        // You can't immute mutability
        // settings.
        case TokenSetting::ModifyAddTokens:
            break;
        case TokenSetting::Revoke:
            result = settings[size_t(TokenSetting::ModifyRevoke)];
            break;
        case TokenSetting::ModifyRevoke:
            break;
        case TokenSetting::Freeze:
            result = settings[size_t(TokenSetting::ModifyFreeze)];
            break;
        case TokenSetting::ModifyFreeze:
            break;
        case TokenSetting::AdjustFee:
            result = settings[size_t(TokenSetting::ModifyAdjustFee)];
            break;
        case TokenSetting::ModifyAdjustFee:
            break;
        case TokenSetting::Whitelist:
            result = settings[size_t(TokenSetting::ModifyWhitelist)];
            break;
        case TokenSetting::ModifyWhitelist:
            break;
        case TokenSetting::Unknown:
            break;
    }

    return result;
}

void TokenAccount::Set(TokenSetting setting, bool value)
{
    auto pos = static_cast<EnumType>(setting);
    settings.set(pos, value);
}

bool TokenAccount::Allowed(TokenSetting setting) const
{
    return settings.test(static_cast<EnumType>(setting));
}

bool TokenAccount::IsMutabilitySetting(TokenSetting setting) const
{
    // Enum values for mutability settings
    // are odd numbers.
    //
    return static_cast<EnumType>(setting) % 2;
}

TokenSetting TokenAccount::GetMutabilitySetting(TokenSetting setting) const
{
    // For a given enum value representing
    // a basic setting, the corresponding
    // mutability setting will have a value
    // that is greater by 1.
    //
    return static_cast<TokenSetting>(static_cast<EnumType>(setting) + 1);
}
