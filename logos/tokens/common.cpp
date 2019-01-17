#include <logos/tokens/common.hpp>

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

void TokenRequest::Hash(blake2b_state & hash) const
{
    Request::Hash(hash);

    blake2b_update(&hash, &token_id, sizeof(token_id));
}

uint16_t TokenRequest::WireSize() const
{
    return sizeof(token_id) + Request::WireSize();
}

TokenAdminRequest::TokenAdminRequest(bool & error,
                                     std::basic_streambuf<uint8_t> & stream)
    : TokenRequest(error, stream)
{
    if(error)
    {
        return;
    }

    error = read(stream, admin_address);
}

void TokenAdminRequest::Hash(blake2b_state & hash) const
{
    TokenRequest::Hash(hash);

    blake2b_update(&hash, &admin_address, sizeof(admin_address));
}

uint16_t TokenAdminRequest::WireSize() const
{
    return sizeof(admin_address) + TokenRequest::WireSize();
}

ControllerInfo::ControllerInfo(bool & error,
                               std::basic_streambuf<uint8_t> & stream)
{


    error = logos::read(stream, address);
    if(error)
    {
        return;
    }

    error = logos::read(stream, privileges);
}

TokenTransaction::TokenTransaction(bool & error,
                                   std::basic_streambuf<uint8_t> & stream)
{
    error = logos::read(stream, dest);
    if(error)
    {
        return;
    }

    error = logos::read(stream, amount);
}
