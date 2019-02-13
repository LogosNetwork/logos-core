#include <logos/token/requests.hpp>

#include <logos/request/utility.hpp>
#include <logos/request/fields.hpp>
#include <logos/token/account.hpp>
#include <logos/token/util.hpp>

#include <numeric>

TokenIssuance::TokenIssuance(bool & error,
                             const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }
    
    Request::Hash();
}

TokenIssuance::TokenIssuance(bool & error,
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

    Request::Hash();
}

TokenIssuance::TokenIssuance(bool & error,
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
        total_supply = std::stoul(tree.get<std::string>(TOTAL_SUPPLY));

        fee_type = GetTokenFeeType(error, tree.get<std::string>(FEE_TYPE));
        if(error)
        {
            return;
        }

        fee_rate = std::stoul(tree.get<std::string>(FEE_RATE));

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
        for(const auto & entry : controller_tree)
        {
            ControllerInfo c(error, entry.second);

            if(error)
            {
                return;
            }

            controllers.push_back(c);
        }

        // TODO: info is optional
        //
        issuer_info = tree.get<std::string>(INFO);
        Request::Hash();
    }
    catch (...)
    {
        error = true;
    }
}

logos::AccountType TokenIssuance::GetAccountType() const
{
    return logos::AccountType::LogosAccount;
}

AccountAddress TokenIssuance::GetAccount() const
{
    return origin;
}

AccountAddress TokenIssuance::GetSource() const
{
    // The source account for TokenIssuance
    // requests is atypical with respect
    // to other TokenRequests.
    //
    return origin;
}

boost::property_tree::ptree TokenIssuance::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(SYMBOL, symbol);
    tree.put(NAME, name);
    tree.put(TOTAL_SUPPLY, total_supply);
    tree.put(FEE_TYPE, GetTokenFeeTypeField(fee_type));
    tree.put(FEE_RATE, fee_rate);

    boost::property_tree::ptree settings_tree(
        settings.SerializeJson([](size_t pos)
                               {
                                   return GetTokenSettingField(pos);
                               }));
    tree.add_child(SETTINGS, settings_tree);

    boost::property_tree::ptree controllers_tree;
    for(size_t i = 0; i < controllers.size(); ++i)
    {
        controllers_tree.push_back(std::make_pair("",
            controllers[i].SerializeJson()));
    }
    tree.add_child(CONTROLLERS, controllers_tree);

    tree.put(INFO, issuer_info);

    return tree;
}

uint64_t TokenIssuance::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write(stream, symbol) +
           logos::write(stream, name) +
           logos::write(stream, total_supply) +
           logos::write(stream, fee_type) +
           logos::write(stream, fee_rate) +
           settings.Serialize(stream) +
           SerializeVector(stream, controllers) +
           logos::write<uint16_t>(stream, issuer_info);
}

void TokenIssuance::Deserialize(bool & error, logos::stream & stream)
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
}

void TokenIssuance::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

void TokenIssuance::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    blake2b_update(&hash, symbol.data(), symbol.size());
    blake2b_update(&hash, name.data(), name.size());
    blake2b_update(&hash, &total_supply, sizeof(total_supply));
    blake2b_update(&hash, &fee_type, sizeof(fee_type));
    blake2b_update(&hash, &fee_rate, sizeof(fee_rate));
    settings.Hash(hash);

    for(size_t i = 0; i < controllers.size(); ++i)
    {
        controllers[i].Hash(hash);
    }

    blake2b_update(&hash, issuer_info.data(), issuer_info.size());
}

uint16_t TokenIssuance::WireSize() const
{
    return StringWireSize(symbol) +
           StringWireSize(name) +
           sizeof(total_supply) +
           sizeof(fee_type) +
           sizeof(fee_rate) +
           Settings::WireSize() +
           VectorWireSize(controllers) +
           StringWireSize<InfoSizeT>(issuer_info) +
           TokenRequest::WireSize();
}

TokenIssueAdtl::TokenIssueAdtl(bool & error,
                               const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Request::Hash();
}

TokenIssueAdtl::TokenIssueAdtl(bool & error,
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

    Request::Hash();
}

TokenIssueAdtl::TokenIssueAdtl(bool & error,
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
        amount = std::stoul(tree.get<std::string>(AMOUNT));
        Request::Hash();
    }
    catch(...)
    {
        error = true;
    }
}

AccountAddress TokenIssueAdtl::GetSource() const
{
    // The source account for TokenIssueAdtl
    // requests is atypical with respect
    // to other TokenRequests.
    //
    return origin;
}

boost::property_tree::ptree TokenIssueAdtl::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(AMOUNT, amount);

    return tree;
}

uint64_t TokenIssueAdtl::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write(stream, amount);
}

void TokenIssueAdtl::Deserialize(bool & error, logos::stream & stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, amount);
}

void TokenIssueAdtl::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

void TokenIssueAdtl::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    blake2b_update(&hash, &amount, sizeof(amount));
}

uint16_t TokenIssueAdtl::WireSize() const
{
    return sizeof(amount) +
           TokenRequest::WireSize();
}

TokenChangeSetting::TokenChangeSetting(bool & error,
                                       const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Request::Hash();
}

TokenChangeSetting::TokenChangeSetting(bool & error,
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

    Request::Hash();
}

TokenChangeSetting::TokenChangeSetting(bool & error,
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

        Request::Hash();
    }
    catch(...)
    {
        error = true;
    }
}

boost::property_tree::ptree TokenChangeSetting::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(SETTING, GetTokenSettingField(setting));
    tree.put(VALUE, bool(value));

    return tree;
}

uint64_t TokenChangeSetting::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write(stream, setting) +
           logos::write(stream, value);
}

void TokenChangeSetting::Deserialize(bool & error, logos::stream & stream)
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
}

void TokenChangeSetting::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

void TokenChangeSetting::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    blake2b_update(&hash, &setting, sizeof(setting));
    blake2b_update(&hash, &value, sizeof(value));
}

uint16_t TokenChangeSetting::WireSize() const
{
    return sizeof(setting) +
           sizeof(value) +
           TokenRequest::WireSize();
}

TokenImmuteSetting::TokenImmuteSetting(bool & error,
                                       const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Request::Hash();
}

TokenImmuteSetting::TokenImmuteSetting(bool & error,
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

    Request::Hash();
}

TokenImmuteSetting::TokenImmuteSetting(bool & error,
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

        Request::Hash();
    }
    catch(...)
    {
        error = true;
    }
}

boost::property_tree::ptree TokenImmuteSetting::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(SETTING, GetTokenSettingField(setting));

    return tree;
}

uint64_t TokenImmuteSetting::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write(stream, setting);
}

void TokenImmuteSetting::Deserialize(bool & error, logos::stream & stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, setting);
}

void TokenImmuteSetting::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

void TokenImmuteSetting::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    blake2b_update(&hash, &setting, sizeof(setting));
}

uint16_t TokenImmuteSetting::WireSize() const
{
    return sizeof(setting) +
           TokenRequest::WireSize();
}

TokenRevoke::TokenRevoke(bool & error,
                         const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Request::Hash();
}

TokenRevoke::TokenRevoke(bool & error,
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

    Request::Hash();
}

TokenRevoke::TokenRevoke(bool & error,
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

        Request::Hash();
    }
    catch(...)
    {
        error = true;
    }
}

AccountAddress TokenRevoke::GetSource() const
{
    // The source account for TokenRevoke
    // requests is atypical with respect
    // to other TokenRequests.
    //
    return source;
}

logos::AccountType TokenRevoke::GetSourceType() const
{
    return logos::AccountType::LogosAccount;
}

uint16_t TokenRevoke::GetTokenTotal() const
{
    return transaction.amount;
}

bool TokenRevoke::Validate(logos::process_return & result,
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

boost::property_tree::ptree TokenRevoke::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(SOURCE, source.to_account());
    tree.add_child(TRANSACTION, transaction.SerializeJson());

    return tree;
}

uint64_t TokenRevoke::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write(stream, source) +
           transaction.Serialize(stream);
}

void TokenRevoke::Deserialize(bool & error, logos::stream & stream)
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
}

void TokenRevoke::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

void TokenRevoke::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    source.Hash(hash);
    transaction.Hash(hash);
}

uint16_t TokenRevoke::WireSize() const
{
    return sizeof(source.bytes) +
           Transaction::WireSize() +
           TokenRequest::WireSize();
}

TokenFreeze::TokenFreeze(bool & error,
                         const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Request::Hash();
}

TokenFreeze::TokenFreeze(bool & error,
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

    Request::Hash();
}

TokenFreeze::TokenFreeze(bool & error,
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

        action = GetFreezeAction(error, tree.get<std::string>(ACTION));
        if(error)
        {
            return;
        }

        Request::Hash();
    }
    catch(...)
    {
        error = true;
    }
}

boost::property_tree::ptree TokenFreeze::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(ACCOUNT, account.to_account());
    tree.put(ACTION, GetFreezeActionField(action));

    return tree;
}

uint64_t TokenFreeze::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write(stream, account) +
           logos::write(stream, action);
}

void TokenFreeze::Deserialize(bool & error, logos::stream & stream)
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

    error = logos::read(stream, action);
}

void TokenFreeze::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

void TokenFreeze::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    account.Hash(hash);
    blake2b_update(&hash, &action, sizeof(action));
}

uint16_t TokenFreeze::WireSize() const
{
    return sizeof(account.bytes) +
           sizeof(action) +
           TokenRequest::WireSize();
}

TokenSetFee::TokenSetFee(bool & error,
                         const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Request::Hash();
}

TokenSetFee::TokenSetFee(bool & error,
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

    Request::Hash();
}

TokenSetFee::TokenSetFee(bool & error,
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

        fee_rate = std::stoul(tree.get<std::string>(FEE_RATE));
        Request::Hash();
    }
    catch(...)
    {
        error = true;
    }
}

boost::property_tree::ptree TokenSetFee::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(FEE_TYPE, GetTokenFeeTypeField(fee_type));
    tree.put(FEE_RATE, fee_rate);

    return tree;
}

uint64_t TokenSetFee::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write(stream, fee_type) +
           logos::write(stream, fee_rate);
}

void TokenSetFee::Deserialize(bool & error, logos::stream & stream)
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
}

void TokenSetFee::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

void TokenSetFee::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    blake2b_update(&hash, &fee_type, sizeof(fee_type));
    blake2b_update(&hash, &fee_rate, sizeof(fee_rate));
}

uint16_t TokenSetFee::WireSize() const
{
    return sizeof(fee_type) +
           sizeof(fee_rate) +
           TokenRequest::WireSize();
}

TokenWhitelist::TokenWhitelist(bool & error,
                               const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Request::Hash();
}

TokenWhitelist::TokenWhitelist(bool & error,
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

    Request::Hash();
}

TokenWhitelist::TokenWhitelist(bool & error,
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

        Request::Hash();
    }
    catch(...)
    {
        error = true;
    }
}

boost::property_tree::ptree TokenWhitelist::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(ACCOUNT, account.to_account());

    return tree;
}

uint64_t TokenWhitelist::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write(stream, account);
}

void TokenWhitelist::Deserialize(bool & error, logos::stream & stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, account);
}

void TokenWhitelist::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

void TokenWhitelist::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);
    account.Hash(hash);
}

uint16_t TokenWhitelist::WireSize() const
{
    return sizeof(account.bytes) +
           TokenRequest::WireSize();
}

TokenIssuerInfo::TokenIssuerInfo(bool & error,
                                 const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Request::Hash();
}

TokenIssuerInfo::TokenIssuerInfo(bool & error,
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

    Request::Hash();
}

TokenIssuerInfo::TokenIssuerInfo(bool & error,
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
        Request::Hash();
    }
    catch(...)
    {
        error = true;
    }
}

boost::property_tree::ptree TokenIssuerInfo::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(INFO, new_info);

    return tree;
}

uint64_t TokenIssuerInfo::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write<uint16_t>(stream, new_info);
}

void TokenIssuerInfo::Deserialize(bool & error, logos::stream & stream)
{
    if(error)
    {
        return;
    }

    error = logos::read<uint16_t>(stream, new_info);
}

void TokenIssuerInfo::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

void TokenIssuerInfo::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);
    blake2b_update(&hash, new_info.data(), new_info.size());
}

uint16_t TokenIssuerInfo::WireSize() const
{
    return StringWireSize<InfoSizeT>(new_info) +
           TokenRequest::WireSize();
}

TokenController::TokenController(bool & error,
                                 const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Request::Hash();
}

TokenController::TokenController(bool & error,
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

    Request::Hash();
}

TokenController::TokenController(bool & error,
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

        Request::Hash();
    }
    catch(...)
    {
        error = true;
    }
}

bool TokenController::Validate(logos::process_return & result) const
{
    if(action == ControllerAction::Unknown)
    {
        result.code = logos::process_result::invalid_controller_action;
        return false;
    }

    return true;
};

bool TokenController::Validate(logos::process_return & result,
                               std::shared_ptr<logos::Account> info) const
{
    auto token_account = std::static_pointer_cast<TokenAccount>(info);

    if(action == ControllerAction::Add)
    {
        if(token_account->controllers.size() == TokenAccount::MAX_CONTROLLERS)
        {
            result.code = logos::process_result::controller_capacity;
            return false;
        }
    }
    else if(action == ControllerAction::Remove)
    {
        // TODO: Find controllers via TokenAccount
        //       member function.
        auto entry = std::find_if(token_account->controllers.begin(),
                                  token_account->controllers.end(),
                                  [this](const ControllerInfo & c)
                                  {
                                      return c.account == controller.account;
                                  });

        if(entry == token_account->controllers.end())
        {
            result.code = logos::process_result::invalid_controller;
            return false;
        }
    }

    return true;
}

boost::property_tree::ptree TokenController::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(ACTION, GetControllerActionField(action));
    tree.add_child(CONTROLLER, controller.SerializeJson());

    return tree;
}

uint64_t TokenController::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write(stream, action) +
           controller.Serialize(stream);
}

void TokenController::Deserialize(bool & error, logos::stream & stream)
{
    error = logos::read(stream, action);
    if(error)
    {
        return;
    }

    controller = ControllerInfo(error, stream);
}

void TokenController::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

void TokenController::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    blake2b_update(&hash, &action, sizeof(action));
    controller.Hash(hash);
}

uint16_t TokenController::WireSize() const
{
    return sizeof(action) +
           ControllerInfo::WireSize() +
           TokenRequest::WireSize();
}

TokenBurn::TokenBurn(bool & error,
                     const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Request::Hash();
}

TokenBurn::TokenBurn(bool & error,
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

    Request::Hash();
}

TokenBurn::TokenBurn(bool & error,
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
        amount = std::stoul(tree.get<std::string>(AMOUNT));
        Request::Hash();
    }
    catch(...)
    {
        error = true;
    }
}

uint16_t TokenBurn::GetTokenTotal() const
{
    return amount;
}

logos::AccountType TokenBurn::GetSourceType() const
{
    return logos::AccountType::LogosAccount;
}

bool TokenBurn::Validate(logos::process_return & result,
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

boost::property_tree::ptree TokenBurn::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.put(AMOUNT, amount);

    return tree;
}

uint64_t TokenBurn::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           logos::write(stream, amount);
}

void TokenBurn::Deserialize(bool & error, logos::stream & stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, amount);
}

void TokenBurn::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

void TokenBurn::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    blake2b_update(&hash, &amount, sizeof(amount));
}

uint16_t TokenBurn::WireSize() const
{
    return sizeof(amount) +
           TokenRequest::WireSize();
}

TokenAccountSend::TokenAccountSend(bool & error,
                                   const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Request::Hash();
}

TokenAccountSend::TokenAccountSend(bool & error,
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

    Request::Hash();
}

TokenAccountSend::TokenAccountSend(bool & error,
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

    Request::Hash();
}


uint16_t TokenAccountSend::GetTokenTotal() const
{
    return transaction.amount;
}

logos::AccountType TokenAccountSend::GetSourceType() const
{
    return logos::AccountType::TokenAccount;
}

bool TokenAccountSend::Validate(logos::process_return & result,
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

boost::property_tree::ptree TokenAccountSend::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.add_child(TRANSACTION, transaction.SerializeJson());

    return tree;
}

uint64_t TokenAccountSend::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           transaction.Serialize(stream);
}

void TokenAccountSend::Deserialize(bool & error, logos::stream & stream)
{
    transaction = Transaction(error, stream);
}

void TokenAccountSend::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

void TokenAccountSend::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);
    transaction.Hash(hash);
}

uint16_t TokenAccountSend::WireSize() const
{
    return Transaction::WireSize() +
           TokenRequest::WireSize();
}

TokenAccountWithdrawFee::TokenAccountWithdrawFee(bool & error,
                                                 const logos::mdb_val & mdbval)
{
    logos::bufferstream stream(reinterpret_cast<uint8_t const *>(mdbval.data()),
                               mdbval.size());

    DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Request::Hash();
}

TokenAccountWithdrawFee::TokenAccountWithdrawFee(bool & error,
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

    Request::Hash();
}

TokenAccountWithdrawFee::TokenAccountWithdrawFee(bool & error,
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

    Request::Hash();
}

uint16_t TokenAccountWithdrawFee::GetTokenTotal() const
{
    return transaction.amount;
}

logos::AccountType TokenAccountWithdrawFee::GetSourceType() const
{
    return logos::AccountType::TokenAccount;
}

bool TokenAccountWithdrawFee::Validate(logos::process_return & result,
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

boost::property_tree::ptree TokenAccountWithdrawFee::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree = TokenRequest::SerializeJson();

    tree.add_child(TRANSACTION, transaction.SerializeJson());

    return tree;
}

uint64_t TokenAccountWithdrawFee::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           transaction.Serialize(stream);
}

void TokenAccountWithdrawFee::Deserialize(bool & error, logos::stream & stream)
{
    transaction = Transaction(error, stream);
}

void TokenAccountWithdrawFee::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

void TokenAccountWithdrawFee::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);
    transaction.Hash(hash);
}

uint16_t TokenAccountWithdrawFee::WireSize() const
{
    return Transaction::WireSize() +
           TokenRequest::WireSize();
}

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

    Request::Hash();
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

    Request::Hash();
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

            transactions.push_back(t);
        }

        token_fee = std::stoul(tree.get<std::string>(TOKEN_FEE));
        Request::Hash();
    }
    catch(...)
    {
        error = true;
    }
}

uint16_t TokenSend::GetTokenTotal() const
{
    auto total = std::accumulate(transactions.begin(), transactions.end(),
                                 uint16_t(0),
                                 [](const uint16_t a, const Transaction & t)
                                 {
                                     return a + t.amount;
                                 });

    return total + token_fee;
}

logos::AccountType TokenSend::GetSourceType() const
{
    return logos::AccountType::LogosAccount;
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

    tree.put(TOKEN_FEE, token_fee);

    return tree;
}

uint64_t TokenSend::Serialize(logos::stream & stream) const
{
    return TokenRequest::Serialize(stream) +
           SerializeVector(stream, transactions) +
           logos::write(stream, token_fee);
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
}

void TokenSend::DeserializeDB(bool & error, logos::stream & stream)
{
    TokenRequest::DeserializeDB(error, stream);
    if(error)
    {
        return;
    }

    Deserialize(error, stream);
}

void TokenSend::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    for(size_t i = 0; i < transactions.size(); ++i)
    {
        transactions[i].Hash(hash);
    }

    blake2b_update(&hash, &token_fee, sizeof(token_fee));
}

uint16_t TokenSend::WireSize() const
{
    return VectorWireSize(transactions) +
           sizeof(token_fee) +
           TokenRequest::WireSize();
}
