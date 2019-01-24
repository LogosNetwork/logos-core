#include <logos/token/account.hpp>

#include <ios>

bool TokenAccount::Validate(TokenSetting setting, bool value, logos::process_return & result) const
{
    auto pos = static_cast<EnumType>(setting);
    bool cur_val = _settings.test(pos);

    // Settings with odd values represent the
    // mutability of the previous setting.
    if(IsMutabilitySetting(setting))
    {
        // Mutability is false.
        if(!cur_val)
        {
            LOG_ERROR(_log) << "Attempt to update a false mutability setting: "
                            << TokenSettingName(setting);

            result.code = logos::process_result::revert_immutability;
            return false;
        }
    }
    else
    {
        auto ms = static_cast<EnumType>(GetMutabilitySetting(setting));

        if(!_settings.test(ms))
        {
            LOG_ERROR(_log) << "Attempt to update immutable setting: "
                            << TokenSettingName(setting);

            result.code = logos::process_result::immutable;
            return false;;
        }
    }

    if(cur_val == value)
    {
        LOG_WARN(_log) << "Redundantly setting ("
                       << TokenSettingName(setting)
                       << ") to "
                       << std::boolalpha << value;

        result.code = logos::process_result::redundant;
    }

    result.code = logos::process_result::progress;
    return true;
}

void TokenAccount::Set(TokenSetting setting, bool value)
{
    auto pos = static_cast<EnumType>(setting);
    _settings.set(pos, value);
}

bool TokenAccount::Allowed(TokenSetting setting) const
{
    return _settings.test(static_cast<EnumType>(setting));
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
