#include <logos/tokens/requests.hpp>

#include <logos/lib/blocks.hpp>

TokenIssuance::TokenIssuance(bool & error,
                             std::basic_streambuf<uint8_t> & stream)
    : TokenAdminRequest(error, stream)
{
    if(error)
    {
        return;
    }

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

    error = logos::read(stream, settings);
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

void TokenIssuance::Hash(blake2b_state & hash) const
{
    TokenAdminRequest::Hash(hash);

    blake2b_update(&hash, &symbol,       sizeof(symbol));
    blake2b_update(&hash, &name,         sizeof(name));
    blake2b_update(&hash, &total_supply, sizeof(total_supply));
    blake2b_update(&hash, &settings,     sizeof(settings));
    blake2b_update(&hash, &controllers,  sizeof(controllers));
    blake2b_update(&hash, &issuer_info,  sizeof(issuer_info));
}

uint16_t TokenIssuance::WireSize() const
{
    return StringWireSize(symbol) +
           StringWireSize(name) +
           sizeof(total_supply) +
           sizeof(settings) +
           VectorWireSize(controllers) +
           StringWireSize(issuer_info) +
           TokenAdminRequest::WireSize();
}

TokenIssueAdd::TokenIssueAdd(bool & error,
                             std::basic_streambuf<uint8_t> & stream)
    : TokenAdminRequest(error, stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, amount);
}

void TokenIssueAdd::Hash(blake2b_state & hash) const
{
    TokenAdminRequest::Hash(hash);

    blake2b_update(&hash, &amount, sizeof(amount));
}

uint16_t TokenIssueAdd::WireSize() const
{
    return sizeof(amount) +
           TokenAdminRequest::WireSize();
}

TokenChangeSetting::TokenChangeSetting(bool & error,
                                       std::basic_streambuf<uint8_t> & stream)
    : TokenAdminRequest(error, stream)
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

void TokenChangeSetting::Hash(blake2b_state & hash) const
{
    TokenAdminRequest::Hash(hash);

    blake2b_update(&hash, &setting,  sizeof(setting));
    blake2b_update(&hash, &value,    sizeof(value));
}

uint16_t TokenChangeSetting::WireSize() const
{
    return sizeof(setting) +
           sizeof(value) +
           TokenAdminRequest::WireSize();
}

TokenImmuteSetting::TokenImmuteSetting(bool & error,
                                       std::basic_streambuf<uint8_t> & stream)
    : TokenAdminRequest(error, stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, setting);
}

void TokenImmuteSetting::Hash(blake2b_state & hash) const
{
    TokenAdminRequest::Hash(hash);

    blake2b_update(&hash, &setting, sizeof(setting));
}

uint16_t TokenImmuteSetting::WireSize() const
{
    return sizeof(setting) +
           TokenAdminRequest::WireSize();
}

TokenRevoke::TokenRevoke(bool & error,
                         std::basic_streambuf<uint8_t> & stream)
    : TokenAdminRequest(error, stream)
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

    error = logos::read(stream, dest);
    if(error)
    {
        return;
    }

    error = logos::read(stream, amount);
}

void TokenRevoke::Hash(blake2b_state & hash) const
{
    TokenAdminRequest::Hash(hash);

    blake2b_update(&hash, &source, sizeof(source));
    blake2b_update(&hash, &dest, sizeof(dest));
    blake2b_update(&hash, &amount, sizeof(amount));
}

uint16_t TokenRevoke::WireSize() const
{
    return sizeof(source) +
           sizeof(dest) +
           sizeof(amount) +
           TokenAdminRequest::WireSize();
}

TokenFreeze::TokenFreeze(bool & error,
                         std::basic_streambuf<uint8_t> & stream)
    : TokenAdminRequest(error, stream)
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

void TokenFreeze::Hash(blake2b_state & hash) const
{
    TokenAdminRequest::Hash(hash);

    blake2b_update(&hash, &account, sizeof(account));
    blake2b_update(&hash, &action, sizeof(action));
}

uint16_t TokenFreeze::WireSize() const
{
    return sizeof(account) +
           sizeof(action) +
           TokenAdminRequest::WireSize();
}

TokenSetFee::TokenSetFee(bool & error,
                         std::basic_streambuf<uint8_t> & stream)
    : TokenAdminRequest(error, stream)
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

void TokenSetFee::Hash(blake2b_state & hash) const
{
    TokenAdminRequest::Hash(hash);

    blake2b_update(&hash, &fee_type, sizeof(fee_type));
    blake2b_update(&hash, &fee_rate, sizeof(fee_rate));
}

uint16_t TokenSetFee::WireSize() const
{
    return sizeof(fee_type) +
           sizeof(fee_rate) +
           TokenAdminRequest::WireSize();
}

TokenWhitelistAdmin::TokenWhitelistAdmin(bool & error,
                                         std::basic_streambuf<uint8_t> & stream)
    : TokenAdminRequest(error, stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, account);
}

void TokenWhitelistAdmin::Hash(blake2b_state & hash) const
{
    TokenAdminRequest::Hash(hash);

    blake2b_update(&hash, &account, sizeof(account));
}

uint16_t TokenWhitelistAdmin::WireSize() const
{
    return sizeof(account) +
           TokenAdminRequest::WireSize();
}

TokenIssuerInfo::TokenIssuerInfo(bool & error,
                                 std::basic_streambuf<uint8_t> & stream)
    : TokenAdminRequest(error, stream)
{
    if(error)
    {
        return;
    }

    error = logos::read<uint16_t>(stream, new_info);
}

void TokenIssuerInfo::Hash(blake2b_state & hash) const
{
    TokenAdminRequest::Hash(hash);

    blake2b_update(&hash, &new_info, sizeof(new_info));
}

uint16_t TokenIssuerInfo::WireSize() const
{
    return StringWireSize(new_info) +
           TokenAdminRequest::WireSize();
}

TokenController::TokenController(bool & error,
                                 std::basic_streambuf<uint8_t> & stream)
    : TokenAdminRequest(error, stream)
    , controller(error, stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, action);
}

void TokenController::Hash(blake2b_state & hash) const
{
    TokenAdminRequest::Hash(hash);

    blake2b_update(&hash, &action,     sizeof(action));
    blake2b_update(&hash, &controller, sizeof(controller));
}

uint16_t TokenController::WireSize() const
{
    return sizeof(action) +
           sizeof(controller) +
           TokenAdminRequest::WireSize();
}

TokenBurn::TokenBurn(bool & error,
                     std::basic_streambuf<uint8_t> & stream)
    : TokenAdminRequest(error, stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, amount);
}

void TokenBurn::Hash(blake2b_state & hash) const
{
    TokenAdminRequest::Hash(hash);

    blake2b_update(&hash, &amount, sizeof(amount));
}

uint16_t TokenBurn::WireSize() const
{
    return sizeof(amount) +
           TokenAdminRequest::WireSize();
}

TokenAccountSend::TokenAccountSend(bool & error,
                                   std::basic_streambuf<uint8_t> & stream)
    : TokenAdminRequest(error, stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, dest);
    if(error)
    {
        return;
    }

    error = logos::read(stream, amount);}

void TokenAccountSend::Hash(blake2b_state & hash) const
{
    TokenAdminRequest::Hash(hash);

    blake2b_update(&hash, &dest,   sizeof(dest));
    blake2b_update(&hash, &amount, sizeof(amount));
}

uint16_t TokenAccountSend::WireSize() const
{
    return sizeof(dest) +
           sizeof(amount) +
           TokenAdminRequest::WireSize();
}

TokenAccountWithdrawFee::TokenAccountWithdrawFee(bool & error,
                                                 std::basic_streambuf<uint8_t> & stream)
    : TokenAdminRequest(error, stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, dest);
    if(error)
    {
        return;
    }


    error = logos::read(stream, amount);
}

void TokenAccountWithdrawFee::Hash(blake2b_state & hash) const
{
    TokenAdminRequest::Hash(hash);

    blake2b_update(&hash, &dest,   sizeof(dest));
    blake2b_update(&hash, &amount, sizeof(amount));
}

uint16_t TokenAccountWithdrawFee::WireSize() const
{
    return sizeof(dest) +
           sizeof(amount) +
           TokenAdminRequest::WireSize();
}

TokenSend::TokenSend(bool & error,
                     std::basic_streambuf<uint8_t> & stream)
    : TokenRequest(error, stream)
{
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

    for(uint8_t i = 0; i < len; ++i)
    {
        TokenTransaction t(error, stream);
        if(error)
        {
            return;
        }

        transactions.push_back(t);
    }

    error = logos::read(stream, fee);
}

void TokenSend::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    blake2b_update(&hash, &transactions, sizeof(transactions));
    blake2b_update(&hash, &fee,          sizeof(fee));
}

uint16_t TokenSend::WireSize() const
{
    return VectorWireSize(transactions) +
           sizeof(fee) +
           TokenRequest::WireSize();
}
