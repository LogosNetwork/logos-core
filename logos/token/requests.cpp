#include <logos/token/requests.hpp>

#include <logos/request/utility.hpp>
#include <logos/request/fields.hpp>
#include <logos/token/account.hpp>
#include <logos/token/utility.hpp>

#include <numeric>
#include <algorithm>

Issuance::Issuance()
    : TokenRequest(RequestType::Issuance)
{}

Issuance::Issuance(bool & error,
                   const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

Issuance::Issuance(bool & error,
                   std::basic_streambuf<uint8_t> & stream)
    : TokenRequest(error, stream)
{
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

Issuance::Issuance(bool & error,
                   boost::property_tree::ptree const & tree)
    : TokenRequest(error, tree)
{
    using namespace request::fields;

    if(error)
    {
        return;
    }

    try
    {
        symbol = tree.get<std::string>(SYMBOL);
        name = tree.get<std::string>(NAME);

        error = total_supply.decode_dec(tree.get<std::string>(TOTAL_SUPPLY));
        if(error)
        {
            return;
        }

        fee_type = GetTokenFeeType(error, tree.get<std::string>(FEE_TYPE));
        if(error)
        {
            return;
        }

        error = fee_rate.decode_dec(tree.get<std::string>(FEE_RATE));
        if(error)
        {
            return;
        }

        auto settings_tree = tree.get_child(SETTINGS);
        settings.DeserializeJson(error, settings_tree,
                                 [](bool & error, const std::string & data)
                                 {
                                     return size_t(GetTokenSetting(error, data));
                                 });
        if(error)
        {
            return;
        }

        auto controller_tree = tree.get_child(CONTROLLERS);
        vector<std::string> controller_accounts;
        for(const auto & entry : controller_tree)
        {
            ControllerInfo c(error, entry.second);
            if(error)
            {
                return;
            }
            controller_accounts.push_back(entry.second.get<std::string>(ACCOUNT));
            controllers.push_back(c);
        }

        // SG: Check for repeating controller accounts in single issuance request
        std::sort(controller_accounts.begin(), controller_accounts.end());
        error = std::unique(controller_accounts.begin(), controller_accounts.end()) != controller_accounts.end();
        if (error)
        {
            return;
        }

        issuer_info = tree.get<std::string>(ISSUER_INFO, "");

        token_id = GetTokenID(symbol, name, origin, previous);
        SignAndHash(error, tree);
    }
    catch (...)
    {
        error = true;
    }
}

bool Issuance::Validate(logos::process_return & result) const
{
    if(!TokenRequest::Validate(result))
    {
        return false;
    }

    auto is_alphanumeric = [](const auto & str)
    {
        for(auto c : str)
        {
            if(!std::isalnum(c))
            {
                return false;
            }
        }

        return true;
    };

    // SG: Token names allowed to include space, hyphen, underscore
    auto is_valid_name = [](const auto & str)
    {
        for(auto c : str)
        {
            if(!(std::isalnum(c) or std::isspace(c) or c=='-' or c=='_'))
            {
                return false;
            }
        }

        return true;
    };

    if(token_id != GetTokenID(*this))
    {
        result.code = logos::process_result::invalid_token_id;
        return false;
    }

    if(symbol.empty() || !is_alphanumeric(symbol) || symbol.size() > SYMBOL_MAX_SIZE)
    {
        result.code = logos::process_result::invalid_token_symbol;
        return false;
    }

    if(name.empty() || !is_valid_name(name) || name.size() > NAME_MAX_SIZE)
    {
        result.code = logos::process_result::invalid_token_name;
        return false;
    }

    if(!ValidateTokenAmount(total_supply))
    {
        result.code = logos::process_result::invalid_token_amount;
        return false;
    }

    if(!ValidateFee(fee_type, fee_rate))
    {
        result.code = logos::process_result::invalid_fee;
        return false;
    }

    if(controllers.size() > TokenAccount::MAX_CONTROLLERS)
    {
        result.code = logos::process_result::controller_capacity;
        return false;
    }

    if(issuer_info.size() > Issuance::INFO_MAX_SIZE)
    {
        result.code = logos::process_result::invalid_issuer_info;
        return false;
    }

    return true;
}

logos::AccountType Issuance::GetAccountType() const
{
    return logos::AccountType::LogosAccount;
}

AccountAddress Issuance::GetAccount() const
{
    return origin;
}

AccountAddress Issuance::GetSource() const
{
    // The source account for Issuance
    // requests is atypical with respect
    // to other TokenRequests.
    //
    return origin;
}

boost::property_tree::ptree Issuance::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(SYMBOL, symbol);
    tree.put(NAME, name);
    tree.put(TOTAL_SUPPLY, total_supply.to_string_dec());
    tree.put(FEE_TYPE, GetTokenFeeTypeField(fee_type));
    tree.put(FEE_RATE, fee_rate.to_string_dec());

    boost::property_tree::ptree settings_tree(
        settings.SerializeJson([](size_t pos)
                               {
                                   return GetTokenSettingField(pos);
                               }));
    // SG: maintain consistent data structure for JSON, no settings is empty array
    if(settings_tree.empty())
    {
        tree.put(SETTINGS, "[]");
    }
    else
    {
        tree.add_child(SETTINGS, settings_tree);
    }

    boost::property_tree::ptree controllers_tree;
    for(size_t i = 0; i < controllers.size(); ++i)
    {
        controllers_tree.push_back(std::make_pair("",
                                                  controllers[i].SerializeJson()));
    }
    tree.add_child(CONTROLLERS, controllers_tree);

    tree.put(ISSUER_INFO, issuer_info);

    return tree;
}

uint64_t Issuance::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write(stream, symbol) +
           logos::write(stream, name) +
           logos::write(stream, total_supply) +
           logos::write(stream, fee_type) +
           logos::write(stream, fee_rate) +
           settings.Serialize(stream) +
           SerializeVector(stream, controllers) +
           logos::write<uint16_t>(stream, issuer_info) +
           logos::write(stream, signature);
}

void Issuance::Deserialize(bool & error, logos::stream & stream)
{
    error = logos::read(stream, symbol);
    if(error)
    {
        return;
    }

    error = logos::read(stream, name);
    if(error)
    {
        return;
    }

    error = logos::read(stream, total_supply);
    if(error)
    {
        return;
    }

    error = logos::read(stream, fee_type);
    if(error)
    {
        return;
    }

    error = logos::read(stream, fee_rate);
    if(error)
    {
        return;
    }

    error = settings.Deserialize(stream);
    if(error)
    {
        return;
    }

    uint8_t len;
    error = logos::read(stream, len);
    if(error)
    {
        return;
    }

    for(size_t i = 0; i < len; ++i)
    {
        ControllerInfo c(error, stream);
        if(error)
        {
            return;
        }

        controllers.push_back(c);
    }

    error = logos::read<uint16_t>(stream, issuer_info);
    if(error)
    {
        return;
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
        if(error)
        {
            return;
        }
    }
}

void Issuance::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    error = logos::read(stream, next);
}

void Issuance::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    blake2b_update(&hash, symbol.data(), symbol.size());
    blake2b_update(&hash, name.data(), name.size());
    total_supply.Hash(hash);
    blake2b_update(&hash, &fee_type, sizeof(fee_type));
    fee_rate.Hash(hash);
    settings.Hash(hash);

    for(size_t i = 0; i < controllers.size(); ++i)
    {
        controllers[i].Hash(hash);
    }

    blake2b_update(&hash, issuer_info.data(), issuer_info.size());
}

uint16_t Issuance::WireSize() const
{
    return StringWireSize(symbol) +
           StringWireSize(name) +
           sizeof(total_supply.bytes) +
           sizeof(fee_type) +
           sizeof(fee_rate.bytes) +
           Settings::WireSize() +
           VectorWireSize(controllers) +
           StringWireSize<InfoSizeT>(issuer_info) +
           TokenRequest::WireSize();
}

bool Issuance::operator==(const Request & other) const
{
    try
    {
        auto derived = dynamic_cast<const Issuance &>(other);

        return Request::operator==(other) &&
               symbol == derived.symbol &&
               name == derived.name &&
               total_supply == derived.total_supply &&
               fee_type == derived.fee_type &&
               fee_rate == derived.fee_rate &&
               settings == derived.settings &&
               controllers == derived.controllers &&
               issuer_info == derived.issuer_info;
    }
    catch(...)
    {}

    return false;
}

IssueAdditional::IssueAdditional()
    : TokenRequest(RequestType::IssueAdditional)
{}

IssueAdditional::IssueAdditional(bool & error,
                                 const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

IssueAdditional::IssueAdditional(bool & error,
                                 std::basic_streambuf<uint8_t> & stream)
    : TokenRequest(error, stream)
{
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

IssueAdditional::IssueAdditional(bool & error,
                                 boost::property_tree::ptree const & tree)
    : TokenRequest(error, tree)
{
    using namespace request::fields;

    if(error)
    {
        return;
    }

    try
    {
        error = amount.decode_dec(tree.get<std::string>(AMOUNT));
        if(error)
        {
            return;
        }

        SignAndHash(error, tree);
    }
    catch(...)
    {
        error = true;
    }
}

bool IssueAdditional::Validate(logos::process_return & result) const
{
    if(!TokenRequest::Validate(result))
    {
        return false;
    }

    if(!ValidateTokenAmount(amount))
    {
        result.code = logos::process_result::invalid_token_amount;
        return false;
    }

    return true;
}

bool IssueAdditional::Validate(logos::process_return & result,
                               std::shared_ptr<logos::Account> info) const
{
    auto token_account = std::static_pointer_cast<TokenAccount>(info);

    if(token_account->total_supply + amount < token_account->total_supply)
    {
        result.code = logos::process_result::total_supply_overflow;
        return false;
    }

    return true;
}

AccountAddress IssueAdditional::GetSource() const
{
    // The source account for IssueAdditional
    // requests is atypical with respect
    // to other TokenRequests.
    //
    return origin;
}

boost::property_tree::ptree IssueAdditional::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(AMOUNT, amount.to_string_dec());

    return tree;
}

uint64_t IssueAdditional::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write(stream, amount) +
           logos::write(stream, signature);
}

void IssueAdditional::Deserialize(bool & error, logos::stream & stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, amount);
    if(error)
    {
        return;
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
        if(error)
        {
            return;
        }
    }
}

void IssueAdditional::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    error = logos::read(stream, next);
}

void IssueAdditional::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);
    amount.Hash(hash);
}

uint16_t IssueAdditional::WireSize() const
{
    return sizeof(amount.bytes) +
           TokenRequest::WireSize();
}

bool IssueAdditional::operator==(const Request & other) const
{
    try
    {
        auto derived = dynamic_cast<const IssueAdditional &>(other);

        return Request::operator==(other) &&
               amount == derived.amount;
    }
    catch(...)
    {}

    return false;
}

ChangeSetting::ChangeSetting()
    : TokenRequest(RequestType::ChangeSetting)
{}

ChangeSetting::ChangeSetting(bool & error,
                             const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

ChangeSetting::ChangeSetting(bool & error,
                             std::basic_streambuf<uint8_t> & stream)
    : TokenRequest(error, stream)
{
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

ChangeSetting::ChangeSetting(bool & error,
                             boost::property_tree::ptree const & tree)
    : TokenRequest(error, tree)
{
    using namespace request::fields;

    if(error)
    {
        return;
    }

    try
    {
        setting = GetTokenSetting(error, tree.get<std::string>(SETTING));
        if(error)
        {
            return;
        }

        value = tree.get<bool>(VALUE) ?
                SettingValue::Enabled :
                SettingValue::Disabled;

        SignAndHash(error, tree);
    }
    catch(...)
    {
        error = true;
    }
}

boost::property_tree::ptree ChangeSetting::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(SETTING, GetTokenSettingField(setting));
    tree.put(VALUE, bool(value));

    return tree;
}

uint64_t ChangeSetting::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write(stream, setting) +
           logos::write(stream, value) +
           logos::write(stream, signature);
}

void ChangeSetting::Deserialize(bool & error, logos::stream & stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, setting);
    if(error)
    {
        return;
    }

    error = logos::read(stream, value);
    if(error)
    {
        return;
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
        if(error)
        {
            return;
        }
    }
}

void ChangeSetting::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    error = logos::read(stream, next);
}

void ChangeSetting::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    blake2b_update(&hash, &setting, sizeof(setting));
    blake2b_update(&hash, &value, sizeof(value));
}

uint16_t ChangeSetting::WireSize() const
{
    return sizeof(setting) +
           sizeof(value) +
           TokenRequest::WireSize();
}

bool ChangeSetting::operator==(const Request & other) const
{
    try
    {
        auto derived = dynamic_cast<const ChangeSetting &>(other);

        return Request::operator==(other) &&
               setting == derived.setting &&
               value == derived.value;
    }
    catch(...)
    {}

    return false;
}

ImmuteSetting::ImmuteSetting()
    : TokenRequest(RequestType::ImmuteSetting)
{}

ImmuteSetting::ImmuteSetting(bool & error,
                             const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

ImmuteSetting::ImmuteSetting(bool & error,
                             std::basic_streambuf<uint8_t> & stream)
    : TokenRequest(error, stream)
{
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

ImmuteSetting::ImmuteSetting(bool & error,
                             boost::property_tree::ptree const & tree)
    : TokenRequest(error, tree)
{
    using namespace request::fields;

    if(error)
    {
        return;
    }

    try
    {
        setting = GetTokenSetting(error, tree.get<std::string>(SETTING));
        if(error)
        {
            return;
        }

        SignAndHash(error, tree);
    }
    catch(...)
    {
        error = true;
    }
}

bool ImmuteSetting::Validate(logos::process_return & result) const
{
    if(!TokenRequest::Validate(result))
    {
        return false;
    }

    if(TokenAccount::IsMutabilitySetting(setting))
    {
        result.code = logos::process_result::prohibitted_request;
        return false;
    }

    return true;
};

boost::property_tree::ptree ImmuteSetting::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(SETTING, GetTokenSettingField(setting));

    return tree;
}

uint64_t ImmuteSetting::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write(stream, setting) +
           logos::write(stream, signature);
}

void ImmuteSetting::Deserialize(bool & error, logos::stream & stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, setting);
    if(error)
    {
        return;
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
        if(error)
        {
            return;
        }
    }
}

void ImmuteSetting::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    error = logos::read(stream, next);
}

void ImmuteSetting::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    blake2b_update(&hash, &setting, sizeof(setting));
}

uint16_t ImmuteSetting::WireSize() const
{
    return sizeof(setting) +
           TokenRequest::WireSize();
}

bool ImmuteSetting::operator==(const Request & other) const
{
    try
    {
        auto derived = dynamic_cast<const ImmuteSetting &>(other);

        return Request::operator==(other) &&
               setting == derived.setting;
    }
    catch(...)
    {}

    return false;
}

Revoke::Revoke()
    : TokenRequest(RequestType::Revoke)
{}

Revoke::Revoke(bool & error,
               const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

Revoke::Revoke(bool & error,
               std::basic_streambuf<uint8_t> & stream)
    : TokenRequest(error, stream)
{
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

Revoke::Revoke(bool & error,
               boost::property_tree::ptree const & tree)
    : TokenRequest(error, tree)
    , transaction(error,
                  tree.get_child(request::fields::TRANSACTION,
                                 boost::property_tree::ptree()))
{
    using namespace request::fields;

    if(error)
    {
        return;
    }

    try
    {
        error = source.decode_account(tree.get<std::string>(SOURCE));
        if(error)
        {
            return;
        }

        SignAndHash(error, tree);
    }
    catch(...)
    {
        error = true;
    }
}

AccountAddress Revoke::GetSource() const
{
    // The source account for Revoke
    // requests is atypical with respect
    // to other TokenRequests.
    //
    return source;
}

logos::AccountType Revoke::GetSourceType() const
{
    return logos::AccountType::LogosAccount;
}

Amount Revoke::GetTokenTotal() const
{
    return transaction.amount;
}

bool Revoke::Validate(logos::process_return & result) const
{
    if(!TokenRequest::Validate(result))
    {
        return false;
    }

    if(!ValidateTokenAmount(transaction.amount))
    {
        result.code = logos::process_result::invalid_token_amount;
        return false;
    }

    return true;
}

bool Revoke::Validate(logos::process_return & result,
                      std::shared_ptr<logos::Account> info) const
{
    auto user_account = std::static_pointer_cast<logos::account_info>(info);

    TokenEntry entry;
    if(!user_account->GetEntry(token_id, entry))
    {
        result.code = logos::process_result::untethered_account;
        return false;
    }

    if(transaction.amount > entry.balance)
    {
        result.code = logos::process_result::insufficient_token_balance;
        return false;
    }

    return true;
}

boost::property_tree::ptree Revoke::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(SOURCE, source.to_account());
    tree.add_child(TRANSACTION, transaction.SerializeJson());

    return tree;
}

uint64_t Revoke::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write(stream, source) +
           transaction.Serialize(stream) +
           logos::write(stream, signature);
}

void Revoke::Deserialize(bool & error, logos::stream & stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, source);
    if(error)
    {
        return;
    }

    transaction = Transaction(error, stream);
    if(error)
    {
        return;
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
        if(error)
        {
            return;
        }
    }
}

void Revoke::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    error = logos::read(stream, next);
}

void Revoke::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    source.Hash(hash);
    transaction.Hash(hash);
}

uint16_t Revoke::WireSize() const
{
    return sizeof(source.bytes) +
           Transaction::WireSize() +
           TokenRequest::WireSize();
}

bool Revoke::operator==(const Request & other) const
{
    try
    {
        auto derived = dynamic_cast<const Revoke &>(other);

        return Request::operator==(other) &&
               source == derived.source &&
               transaction == derived.transaction;
    }
    catch(...)
    {}

    return false;
}

AdjustUserStatus::AdjustUserStatus()
    : TokenRequest(RequestType::AdjustUserStatus)
{}

AdjustUserStatus::AdjustUserStatus(bool & error,
                                   const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

AdjustUserStatus::AdjustUserStatus(bool & error,
                                   std::basic_streambuf<uint8_t> & stream)
    : TokenRequest(error, stream)
{
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

AdjustUserStatus::AdjustUserStatus(bool & error,
                                   boost::property_tree::ptree const & tree)
    : TokenRequest(error, tree)
{
    using namespace request::fields;

    if(error)
    {
        return;
    }

    try
    {
        error = account.decode_account(tree.get<std::string>(ACCOUNT));
        if(error)
        {
            return;
        }

        status = GetUserStatus(error, tree.get<std::string>(STATUS));
        if(error)
        {
            return;
        }

        SignAndHash(error, tree);
    }
    catch(...)
    {
        error = true;
    }
}

boost::property_tree::ptree AdjustUserStatus::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(ACCOUNT, account.to_account());
    tree.put(STATUS, GetUserStatusField(status));

    return tree;
}

uint64_t AdjustUserStatus::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write(stream, account) +
           logos::write(stream, status) +
           logos::write(stream, signature);
}

void AdjustUserStatus::Deserialize(bool & error, logos::stream & stream)
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

    error = logos::read(stream, status);
    if(error)
    {
        return;
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
        if(error)
        {
            return;
        }
    }
}

void AdjustUserStatus::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    error = logos::read(stream, next);
}

void AdjustUserStatus::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    account.Hash(hash);
    blake2b_update(&hash, &status, sizeof(status));
}

uint16_t AdjustUserStatus::WireSize() const
{
    return sizeof(account.bytes) +
           sizeof(status) +
           TokenRequest::WireSize();
}

bool AdjustUserStatus::operator==(const Request & other) const
{
    try
    {
        auto derived = dynamic_cast<const AdjustUserStatus &>(other);

        return Request::operator==(other) &&
               account == derived.account &&
               status == derived.status;
    }
    catch(...)
    {}

    return false;
}

AdjustFee::AdjustFee()
    : TokenRequest(RequestType::AdjustFee)
{}

AdjustFee::AdjustFee(bool & error,
                     const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

AdjustFee::AdjustFee(bool & error,
                     std::basic_streambuf<uint8_t> & stream)
    : TokenRequest(error, stream)
{
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

AdjustFee::AdjustFee(bool & error,
                     boost::property_tree::ptree const & tree)
    : TokenRequest(error, tree)
{
    using namespace request::fields;

    if(error)
    {
        return;
    }

    try
    {
        fee_type = GetTokenFeeType(error, tree.get<std::string>(FEE_TYPE));
        if(error)
        {
            return;
        }

        error = fee_rate.decode_dec(tree.get<std::string>(FEE_RATE));
        if(error)
        {
            return;
        }

        SignAndHash(error, tree);
    }
    catch(...)
    {
        error = true;
    }
}

bool AdjustFee::Validate(logos::process_return & result) const
{
    if(!TokenRequest::Validate(result))
    {
        return false;
    }

    if(!ValidateFee(fee_type, fee_rate))
    {
        result.code = logos::process_result::invalid_fee;
        return false;
    }

    return true;
}

boost::property_tree::ptree AdjustFee::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(FEE_TYPE, GetTokenFeeTypeField(fee_type));
    tree.put(FEE_RATE, fee_rate.to_string_dec());

    return tree;
}

uint64_t AdjustFee::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write(stream, fee_type) +
           logos::write(stream, fee_rate.bytes) +
           logos::write(stream, signature);
}

void AdjustFee::Deserialize(bool & error, logos::stream & stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, fee_type);
    if(error)
    {
        return;
    }

    error = logos::read(stream, fee_rate);
    if(error)
    {
        return;
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
        if(error)
        {
            return;
        }
    }
}

void AdjustFee::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    error = logos::read(stream, next);
}

void AdjustFee::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    blake2b_update(&hash, &fee_type, sizeof(fee_type));
    fee_rate.Hash(hash);
}

uint16_t AdjustFee::WireSize() const
{
    return sizeof(fee_type) +
           sizeof(fee_rate.bytes) +
           TokenRequest::WireSize();
}

bool AdjustFee::operator==(const Request & other) const
{
    try
    {
        auto derived = dynamic_cast<const AdjustFee &>(other);

        return Request::operator==(other) &&
               fee_type == derived.fee_type &&
               fee_rate == derived.fee_rate;
    }
    catch(...)
    {}

    return false;
}

UpdateIssuerInfo::UpdateIssuerInfo()
    : TokenRequest(RequestType::UpdateIssuerInfo)
{}

UpdateIssuerInfo::UpdateIssuerInfo(bool & error,
                                   const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

UpdateIssuerInfo::UpdateIssuerInfo(bool & error,
                                   std::basic_streambuf<uint8_t> & stream)
    : TokenRequest(error, stream)
{
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

UpdateIssuerInfo::UpdateIssuerInfo(bool & error,
                                   boost::property_tree::ptree const & tree)
    : TokenRequest(error, tree)
{
    using namespace request::fields;

    if(error)
    {
        return;
    }

    try
    {
        new_info = tree.get<std::string>(NEW_INFO);
        SignAndHash(error, tree);
    }
    catch(...)
    {
        error = true;
    }
}

bool UpdateIssuerInfo::Validate(logos::process_return & result) const
{
    if(!TokenRequest::Validate(result))
    {
        return false;
    }

    if(new_info.size() > Issuance::INFO_MAX_SIZE)
    {
        result.code = logos::process_result::invalid_issuer_info;
        return false;
    }

    return true;
};

boost::property_tree::ptree UpdateIssuerInfo::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(NEW_INFO, new_info);

    return tree;
}

uint64_t UpdateIssuerInfo::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write<uint16_t>(stream, new_info) +
           logos::write(stream, signature);
}

void UpdateIssuerInfo::Deserialize(bool & error, logos::stream & stream)
{
    if(error)
    {
        return;
    }

    error = logos::read<uint16_t>(stream, new_info);
    if(error)
    {
        return;
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
        if(error)
        {
            return;
        }
    }
}

void UpdateIssuerInfo::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    error = logos::read(stream, next);
}

void UpdateIssuerInfo::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);
    blake2b_update(&hash, new_info.data(), new_info.size());
}

uint16_t UpdateIssuerInfo::WireSize() const
{
    return StringWireSize<InfoSizeT>(new_info) +
           TokenRequest::WireSize();
}

bool UpdateIssuerInfo::operator==(const Request & other) const
{
    try
    {
        auto derived = dynamic_cast<const UpdateIssuerInfo &>(other);

        return Request::operator==(other) &&
               new_info == derived.new_info;
    }
    catch(...)
    {}

    return false;
}

UpdateController::UpdateController()
    : TokenRequest(RequestType::UpdateController)
{}

UpdateController::UpdateController(bool & error,
                                   const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

UpdateController::UpdateController(bool & error,
                                   std::basic_streambuf<uint8_t> & stream)
    : TokenRequest(error, stream)
{
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

UpdateController::UpdateController(bool & error,
                                   boost::property_tree::ptree const & tree)
    : TokenRequest(error, tree)
{
    using namespace request::fields;

    if(error)
    {
        return;
    }

    try
    {
        action = GetControllerAction(error, tree.get<std::string>(ACTION));
        if(error)
        {
            return;
        }

        controller.DeserializeJson(error, tree.get_child(CONTROLLER));
        if(error)
        {
            return;
        }

        SignAndHash(error, tree);
    }
    catch(...)
    {
        error = true;
    }
}

bool UpdateController::Validate(logos::process_return & result) const
{
    if(!TokenRequest::Validate(result))
    {
        return false;
    }

    switch(action)
    {
        case ControllerAction::Add:
        case ControllerAction::Remove:
            break;
        default:
            result.code = logos::process_result::invalid_controller_action;
            return false;
    }

    return true;
};

bool UpdateController::Validate(logos::process_return & result,
                                std::shared_ptr<logos::Account> info) const
{
    auto token_account = std::static_pointer_cast<TokenAccount>(info);

    ControllerInfo c;
    auto controller_found = token_account->GetController(controller.account, c);

    if(action == ControllerAction::Add)
    {
        // This request will update the privileges
        // of an existing controller.
        if(controller_found)
        {
            return true;
        }

        // This request would cause the token
        // account to exceed the maximum amount
        // of controllers.
        else if(token_account->controllers.size() == TokenAccount::MAX_CONTROLLERS)
        {
            result.code = logos::process_result::controller_capacity;
            return false;
        }
    }
    else if(action == ControllerAction::Remove)
    {
        if(!controller_found)
        {
            result.code = logos::process_result::invalid_controller;
            return false;
        }
    }

    return true;
}

boost::property_tree::ptree UpdateController::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(ACTION, GetControllerActionField(action));
    tree.add_child(CONTROLLER, controller.SerializeJson());

    return tree;
}

uint64_t UpdateController::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write(stream, action) +
           controller.Serialize(stream) +
           logos::write(stream, signature);
}

void UpdateController::Deserialize(bool & error, logos::stream & stream)
{
    error = logos::read(stream, action);
    if(error)
    {
        return;
    }

    controller = ControllerInfo(error, stream);
    if(error)
    {
        return;
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
        if(error)
        {
            return;
        }
    }
}

void UpdateController::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    error = logos::read(stream, next);
}

void UpdateController::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    blake2b_update(&hash, &action, sizeof(action));
    controller.Hash(hash);
}

uint16_t UpdateController::WireSize() const
{
    return sizeof(action) +
           ControllerInfo::WireSize() +
           TokenRequest::WireSize();
}

bool UpdateController::operator==(const Request & other) const
{
    try
    {
        auto derived = dynamic_cast<const UpdateController &>(other);

        return Request::operator==(other) &&
               action == derived.action &&
               controller == derived.controller;
    }
    catch(...)
    {}

    return false;
}

Burn::Burn()
    : TokenRequest(RequestType::Burn)
{}

Burn::Burn(bool & error,
           const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

Burn::Burn(bool & error,
           std::basic_streambuf<uint8_t> & stream)
    : TokenRequest(error, stream)
{
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

Burn::Burn(bool & error,
           boost::property_tree::ptree const & tree)
    : TokenRequest(error, tree)
{
    using namespace request::fields;

    if(error)
    {
        return;
    }

    try
    {
        error = amount.decode_dec(tree.get<std::string>(AMOUNT));
        if(error)
        {
            return;
        }

        SignAndHash(error, tree);
    }
    catch(...)
    {
        error = true;
    }
}

Amount Burn::GetTokenTotal() const
{
    return amount;
}

logos::AccountType Burn::GetSourceType() const
{
    return logos::AccountType::LogosAccount;
}

bool Burn::Validate(logos::process_return & result) const
{
    if(!TokenRequest::Validate(result))
    {
        return false;
    }

    if(!ValidateTokenAmount(amount))
    {
        result.code = logos::process_result::invalid_token_amount;
        return false;
    }

    return true;
}

bool Burn::Validate(logos::process_return & result,
                    std::shared_ptr<logos::Account> info) const
{
    auto token_account = std::static_pointer_cast<TokenAccount>(info);

    if(amount > token_account->token_balance)
    {
        result.code = logos::process_result::insufficient_token_balance;
        return false;
    }

    return true;
}

boost::property_tree::ptree Burn::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(AMOUNT, amount.to_string_dec());

    return tree;
}

uint64_t Burn::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write(stream, amount) +
           logos::write(stream, signature);
}

void Burn::Deserialize(bool & error, logos::stream & stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, amount);
    if(error)
    {
        return;
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
        if(error)
        {
            return;
        }
    }
}

void Burn::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    error = logos::read(stream, next);
}

void Burn::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    amount.Hash(hash);
}

uint16_t Burn::WireSize() const
{
    return sizeof(amount) +
           TokenRequest::WireSize();
}

bool Burn::operator==(const Request & other) const
{
    try
    {
        auto derived = dynamic_cast<const Burn &>(other);

        return Request::operator==(other) &&
               amount == derived.amount;
    }
    catch(...)
    {}

    return false;
}

Distribute::Distribute()
    : TokenRequest(RequestType::Distribute)
{}

Distribute::Distribute(bool & error,
                       const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

Distribute::Distribute(bool & error,
                       std::basic_streambuf<uint8_t> & stream)
    : TokenRequest(error, stream)
{
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

Distribute::Distribute(bool & error,
                       boost::property_tree::ptree const & tree)
    : TokenRequest(error, tree)
    , transaction(error,
                  tree.get_child(request::fields::TRANSACTION,
                                 boost::property_tree::ptree()))
{
    if(error)
    {
        return;
    }

    SignAndHash(error, tree);
}


Amount Distribute::GetTokenTotal() const
{
    return transaction.amount;
}

logos::AccountType Distribute::GetSourceType() const
{
    return logos::AccountType::TokenAccount;
}

AccountAddress Distribute::GetDestination() const
{
    return transaction.destination;
}

bool Distribute::Validate(logos::process_return & result) const
{
    if(!TokenRequest::Validate(result))
    {
        return false;
    }

    if(!ValidateTokenAmount(transaction.amount))
    {
        result.code = logos::process_result::invalid_token_amount;
        return false;
    }

    return true;
}

bool Distribute::Validate(logos::process_return & result,
                          std::shared_ptr<logos::Account> info) const
{
    auto token_account = std::static_pointer_cast<TokenAccount>(info);

    if(transaction.amount > token_account->token_balance)
    {
        result.code = logos::process_result::insufficient_token_balance;
        return false;
    }

    return true;
}

boost::property_tree::ptree Distribute::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.add_child(TRANSACTION, transaction.SerializeJson());

    return tree;
}

uint64_t Distribute::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           transaction.Serialize(stream) +
           logos::write(stream, signature);
}

void Distribute::Deserialize(bool & error, logos::stream & stream)
{
    transaction = Transaction(error, stream);
    if(error)
    {
        return;
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
        if(error)
        {
            return;
        }
    }
}

void Distribute::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    error = logos::read(stream, next);
}

void Distribute::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);
    transaction.Hash(hash);
}

uint16_t Distribute::WireSize() const
{
    return Transaction::WireSize() +
           TokenRequest::WireSize();
}

bool Distribute::operator==(const Request & other) const
{
    try
    {
        auto derived = dynamic_cast<const Distribute &>(other);

        return Request::operator==(other) &&
               transaction == derived.transaction;
    }
    catch(...)
    {}

    return false;
}

WithdrawFee::WithdrawFee()
    : TokenRequest(RequestType::WithdrawFee)
{}

WithdrawFee::WithdrawFee(bool & error,
                         const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

WithdrawFee::WithdrawFee(bool & error,
                         std::basic_streambuf<uint8_t> & stream)
    : TokenRequest(error, stream)
{
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

WithdrawFee::WithdrawFee(bool & error,
                         boost::property_tree::ptree const & tree)
    : TokenRequest(error, tree)
    , transaction(error,
                  tree.get_child(request::fields::TRANSACTION,
                                 boost::property_tree::ptree()))
{
    if(error)
    {
        return;
    }

    SignAndHash(error, tree);
}

Amount WithdrawFee::GetTokenTotal() const
{
    return transaction.amount;
}

logos::AccountType WithdrawFee::GetSourceType() const
{
    return logos::AccountType::TokenAccount;
}

AccountAddress WithdrawFee::GetDestination() const
{
    return transaction.destination;
}

bool WithdrawFee::Validate(logos::process_return & result) const
{
    if(!TokenRequest::Validate(result))
    {
        return false;
    }

    if(!ValidateTokenAmount(transaction.amount))
    {
        result.code = logos::process_result::invalid_token_amount;
        return false;
    }

    return true;
}

bool WithdrawFee::Validate(logos::process_return & result,
                           std::shared_ptr<logos::Account> info) const
{
    auto token_account = std::static_pointer_cast<TokenAccount>(info);

    if(transaction.amount > token_account->token_fee_balance)
    {
        result.code = logos::process_result::insufficient_token_balance;
        return false;
    }

    return true;
}

boost::property_tree::ptree WithdrawFee::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.add_child(TRANSACTION, transaction.SerializeJson());

    return tree;
}

uint64_t WithdrawFee::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           transaction.Serialize(stream) +
           logos::write(stream, signature);
}

void WithdrawFee::Deserialize(bool & error, logos::stream & stream)
{
    transaction = Transaction(error, stream);
    if(error)
    {
        return;
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
        if(error)
        {
            return;
        }
    }
}

void WithdrawFee::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    error = logos::read(stream, next);
}

void WithdrawFee::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);
    transaction.Hash(hash);
}

uint16_t WithdrawFee::WireSize() const
{
    return Transaction::WireSize() +
           TokenRequest::WireSize();
}

bool WithdrawFee::operator==(const Request & other) const
{
    try
    {
        auto derived = dynamic_cast<const WithdrawFee &>(other);

        return Request::operator==(other) &&
               transaction == derived.transaction;
    }
    catch(...)
    {}

    return false;
}

WithdrawLogos::WithdrawLogos()
    : TokenRequest(RequestType::WithdrawLogos)
{}

WithdrawLogos::WithdrawLogos(bool & error,
                             const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

WithdrawLogos::WithdrawLogos(bool & error,
                             std::basic_streambuf<uint8_t> & stream)
    : TokenRequest(error, stream)
{
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

WithdrawLogos::WithdrawLogos(bool & error,
                             boost::property_tree::ptree const & tree)
    : TokenRequest(error, tree)
      , transaction(error,
                    tree.get_child(request::fields::TRANSACTION,
                                   boost::property_tree::ptree()))
{
    if(error)
    {
        return;
    }

    SignAndHash(error, tree);
}

Amount WithdrawLogos::GetTokenTotal() const
{
    return transaction.amount;
}

logos::AccountType WithdrawLogos::GetSourceType() const
{
    return logos::AccountType::TokenAccount;
}

AccountAddress WithdrawLogos::GetDestination() const
{
    return transaction.destination;
}

bool WithdrawLogos::Validate(logos::process_return & result,
                             std::shared_ptr<logos::Account> info) const
{
    if(!TokenRequest::Validate(result))
    {
        return false;
    }

    auto token_account = std::static_pointer_cast<TokenAccount>(info);

    if(transaction.amount > token_account->GetBalance())
    {
        result.code = logos::process_result::insufficient_balance;
        return false;
    }

    return true;
}

boost::property_tree::ptree WithdrawLogos::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.add_child(TRANSACTION, transaction.SerializeJson());

    return tree;
}

uint64_t WithdrawLogos::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           transaction.Serialize(stream) +
           logos::write(stream, signature);
}

void WithdrawLogos::Deserialize(bool & error, logos::stream & stream)
{
    transaction = Transaction(error, stream);
    if(error)
    {
        return;
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
        if(error)
        {
            return;
        }
    }
}

void WithdrawLogos::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    error = logos::read(stream, next);
}

void WithdrawLogos::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);
    transaction.Hash(hash);
}

uint16_t WithdrawLogos::WireSize() const
{
    return Transaction::WireSize() +
           TokenRequest::WireSize();
}

bool WithdrawLogos::operator==(const Request & other) const
{
    try
    {
        auto derived = dynamic_cast<const WithdrawLogos &>(other);

        return Request::operator==(other) &&
               transaction == derived.transaction;
    }
    catch(...)
    {}

    return false;
}

TokenSend::TokenSend()
    : TokenRequest(RequestType::TokenSend)
{}

TokenSend::TokenSend(bool & error,
                     const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

TokenSend::TokenSend(bool & error,
                     std::basic_streambuf<uint8_t> & stream)
    : TokenRequest(error, stream)
{
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    Hash();
}

TokenSend::TokenSend(bool & error,
                     boost::property_tree::ptree const & tree)
    : TokenRequest(error, tree)
{
    using namespace request::fields;

    if(error)
    {
        return;
    }

    try
    {
        auto transactions_tree = tree.get_child(TRANSACTIONS);
        for(auto & entry : transactions_tree)
        {
            Transaction t(error, entry.second);
            if(error)
            {
                return;
            }

            // SG: Added check for maximum token transactions in a request
            error = !AddTransaction(t);
            if(error)
            {
                return;
            }
        }

        error = token_fee.decode_dec(tree.get<std::string>(TOKEN_FEE));
        if(error)
        {
            return;
        }

        SignAndHash(error, tree);
    }
    catch(...)
    {
        error = true;
    }
}

// SG: Same structure as maximum logos transaction check in request
bool TokenSend::AddTransaction(const AccountAddress & to, const Amount & amount)
{
    return AddTransaction(Transaction(to, amount));
}

bool TokenSend::AddTransaction(const Transaction & transaction)
{
    if(transactions.size() < MAX_TRANSACTIONS)
    {
        transactions.push_back(transaction);
        return true;
    }
    return false;
}

Amount TokenSend::GetTokenTotal() const
{
    auto total = std::accumulate(transactions.begin(), transactions.end(),
                                 Amount(0),
                                 [](const Amount a, const Transaction & t)
                                 {
                                     return a + t.amount;
                                 });

    return total + token_fee;
}

logos::AccountType TokenSend::GetSourceType() const
{
    return logos::AccountType::LogosAccount;
}

bool TokenSend::Validate(logos::process_return & result) const
{
    if(!TokenRequest::Validate(result))
    {
        return false;
    }

    for(auto & transaction : transactions)
    {
        if(!ValidateTokenAmount(transaction.amount))
        {
            result.code = logos::process_result::invalid_token_amount;
            return false;
        }
    }

    return true;
}

bool TokenSend::Validate(logos::process_return & result,
                         std::shared_ptr<logos::Account> info) const
{
    auto user_account = std::static_pointer_cast<logos::account_info>(info);

    TokenEntry entry;
    if(!user_account->GetEntry(token_id, entry))
    {
        result.code = logos::process_result::untethered_account;
        return false;
    }

    if(GetTokenTotal() > entry.balance)
    {
        result.code = logos::process_result::insufficient_token_balance;
        return false;
    }

    return true;
}

logos::AccountType TokenSend::GetAccountType() const
{
    return logos::AccountType::LogosAccount;
}

AccountAddress TokenSend::GetAccount() const
{
    return origin;
}

boost::property_tree::ptree TokenSend::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    boost::property_tree::ptree transactions_tree;
    for(size_t i = 0; i < transactions.size(); ++i)
    {
        transactions_tree.push_back(
            std::make_pair("",
                           transactions[i].SerializeJson()));
    }
    tree.add_child(TRANSACTIONS, transactions_tree);

    tree.put(TOKEN_FEE, token_fee.to_string_dec());

    return tree;
}

uint64_t TokenSend::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           SerializeVector(stream, transactions) +
           logos::write(stream, token_fee) +
           logos::write(stream, signature);
}

void TokenSend::Deserialize(bool & error, logos::stream & stream)
{
    uint8_t len;
    error = logos::read(stream, len);
    if(error)
    {
        return;
    }

    for(uint8_t i = 0; i < len; ++i)
    {
        Transaction t(error, stream);
        if(error)
        {
            return;
        }

        transactions.push_back(t);
    }

    error = logos::read(stream, token_fee);
    if(error)
    {
        return;
    }

    error = logos::read(stream, signature);
    if(error)
    {
        return;
    }

    bool with_work;
    error = logos::read(stream, with_work);
    if(error)
    {
        return;
    }

    if(with_work)
    {
        error = logos::read(stream, work);
        if(error)
        {
            return;
        }
    }
}

void TokenSend::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
    if(error)
    {
        return;
    }

    error = logos::read(stream, next);
}

void TokenSend::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    for(size_t i = 0; i < transactions.size(); ++i)
    {
        transactions[i].Hash(hash);
    }

    token_fee.Hash(hash);
}

uint16_t TokenSend::WireSize() const
{
    return VectorWireSize(transactions) +
           sizeof(token_fee) +
           TokenRequest::WireSize();
}

bool TokenSend::operator==(const Request & other) const
{
    try
    {
        auto derived = dynamic_cast<const TokenSend &>(other);

        return Request::operator==(other) &&
               transactions == derived.transactions &&
               token_fee == derived.token_fee;
    }
    catch(...)
    {}

    return false;
}
