// @file
// This file declares TxAcceptorChannel which provides communication between standalone TxAcceptor
// and Delegate

#include <logos/consensus/tx_acceptor/tx_acceptor_channel.hpp>
#include <logos/consensus/tx_acceptor/tx_message_header.hpp>
#include <logos/node/node.hpp>

TxAcceptorChannel::TxAcceptorChannel(Service &service, const std::string & ip, const uint16_t port)
    : _service(service)
    , _endpoint(Endpoint(boost::asio::ip::make_address_v4(ip), port))
    , _delegate(service, _endpoint, *this)
{
    _delegate.Start();
}

void
TxAcceptorChannel::OnConnectionAccepted(const Endpoint endpoint, shared_ptr<Socket> socket)
{
    if (!Validate(endpoint, socket))
    {
        LOG_ERROR(_log) << "TxAcceptorChannel::OnConnectionAccepted failed to validate " << endpoint;
        socket->close();
        return;
    }

    Reset(socket);
}

logos::process_return
TxAcceptorChannel::OnSendRequest(std::shared_ptr<StateBlock> block, bool should_buffer)
{
    logos::process_return result{logos::process_result::progress};

    auto buf{std::make_shared<vector<uint8_t>>()};
    TxMessageHeader header(0, should_buffer);
    {
        logos::vectorstream stream(*buf);
        header.Serialize(stream);
        header.payload_size = block->Serialize(stream);
    }
    HeaderStream header_stream(buf->data(), TxMessageHeader::MESSAGE_SIZE);
    header.Serialize(header_stream);

    if (!AsyncSend(buf))
    {
        result={logos::process_result::initializing};
    }

    LOG_INFO(_log)<< "TxAcceptorChannel::OnSendRequest sent " << header.payload_size;

    return result;
}

void
TxAcceptorChannel::OnError(const Error & error)
{
    LOG_ERROR(_log) << "TxAcceptorChannel::OnError " << error.message();
    _socket->close();
}