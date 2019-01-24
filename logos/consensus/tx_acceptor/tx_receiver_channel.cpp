// @file
// TxReceiverChannel class implements TxChannel to receive transactions from standalone
// TxAcceptor and forward them to ConsensusManager
//

#include <logos/consensus/tx_acceptor/tx_receiver_channel.hpp>
#include <logos/consensus/tx_acceptor/tx_message_header.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/node/node.hpp>

constexpr std::chrono::seconds TxReceiverChannel::CONNECT_RETRY_DELAY;

void
TxReceiverNetIOAssembler::OnError(const Error &error)
{
    _error_handler.ReConnect();
}

TxReceiverChannel::TxReceiverChannel(TxReceiverChannel::Service &service,
                                     logos::alarm & alarm,
                                     const std::string &ip,
                                     const uint16_t port,
                                     TxChannel & receiver)
    : _service(service)
    , _endpoint(Endpoint(boost::asio::ip::make_address_v4(ip), port))
    , _socket(std::make_shared<Socket>(service))
    , _alarm(alarm)
    , _receiver(receiver)
    , _assembler(_socket, *this)
{
    Connect();
}

void
TxReceiverChannel::Connect()
{
   _socket->async_connect(_endpoint,
                           [this](const Error & ec)
						   { OnConnect(ec); });
}

void
TxReceiverChannel::ReConnect()
{
    _socket->close();
    _alarm.add(CONNECT_RETRY_DELAY, [this](){Connect();});
}

void
TxReceiverChannel::OnConnect(const Error & ec)
{
    if (ec)
    {
        LOG_WARN(_log) << "TxReceiverChannel::OnConnect error: " << ec.message();
        ReConnect();
        return;
    }

    AsyncReadHeader();
}

void
TxReceiverChannel::AsyncReadHeader()
{
    _assembler.ReadBytes([this](const uint8_t *data) {
        bool error = false;
        TxMessageHeader header(error, data, TxMessageHeader::MESSAGE_SIZE);
        if (error)
        {
            LOG_ERROR(_log) << "TxReceiverChannel::AsyncReadHeader header deserialize error";
            ReConnect();
            return;
        }
        LOG_INFO(_log) << "TxReceiverChannel::AsyncReadHeader received header";
        AsyncReadMessage(header);
    }, TxMessageHeader::MESSAGE_SIZE);
}

void
TxReceiverChannel::AsyncReadMessage(const TxMessageHeader & header)
{
    using Request = RequestMessage<ConsensusType::BatchStateBlock>;
    auto payload_size = header.payload_size;
    bool should_buffer = header.mpf != 0;
    _assembler.ReadBytes([this, payload_size, should_buffer](const uint8_t *data) {
        bool error = false;
        auto block = std::make_shared<StateBlock>(error, data, payload_size);
        if (error)
        {
            LOG_ERROR(_log) << "TxReceiverChannel::AsyncReadMessage deserialize error, payload size "
                            << payload_size;
            ReConnect();
            return;
        }
        LOG_INFO(_log) << "TxReceiverChannel::AsyncReadMessage received payload size " << payload_size;
        _receiver.OnSendRequest(static_pointer_cast<Request>(block), should_buffer);
        AsyncReadHeader();
    }, payload_size);
}

