//
// Created by gregt on 1/18/19.
//

#include <logos/consensus/tx_acceptor/tx_receiver_channel.hpp>
#include <logos/consensus/tx_acceptor/tx_message.hpp>
#include <logos/node/node.hpp>

constexpr std::chrono::seconds TxReceiverChannel::CONNECT_RETRY_DELAY;

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
    AsyncRead(TxMessage::MESSAGE_SIZE, [this](uint8_t *data) {
        bool error = false;
        TxMessage header(error, data, TxMessage::MESSAGE_SIZE);
        if (error)
        {
            LOG_ERROR(_log) << "TxReceiverChannel::OnConnect header deserialize error";
            ReConnect();
            return;
        }
        AsyncReadMessage(header);
    });
}

void
TxReceiverChannel::AsyncReadMessage(const TxMessage & header)
{
    auto payload_size = header.payload_size;
    AsyncRead(payload_size, [this, payload_size](uint8_t *data) {
        bool error;
        auto block = std::make_shared<StateBlock>(error, data, payload_size);
        OnSendRequest(block);
        AsyncReadHeader();
    });
}

template<typename F>
void
TxReceiverChannel::ProcessCallback(size_t size, F&& f)
{
    f(_buffer.data());
    _buffer_size -= size;
    memmove(_buffer.data(), _buffer.data() + size, _buffer_size);
}

template<typename F>
void
TxReceiverChannel::AsyncRead(size_t requested_size, F&& f)
{
    if (_buffer_size >= requested_size)
    {
        ProcessCallback(requested_size, f);
    }
    else
    {
        auto to_read = requested_size - _buffer_size;

        if (to_read > BUFFER_CAPACITY - _buffer_size)
        {
            LOG_ERROR(_log) << "TxReceiver::AsyncRead requested data is too large " << requested_size;
            ReConnect();
            return;
        }
        boost::asio::async_read(*_socket,
                                boost::asio::buffer(_buffer.data() + _buffer_size, _buffer.size() - _buffer_size),
                                boost::asio::transfer_at_least(to_read),
                                [this, requested_size, f](const Error &ec, size_t size) {
            if (ec)
            {
                LOG_ERROR(_log) << "TxReceiverChannel::AsyncRead error: " << ec.message();
                ReConnect();
                return;
            }
            _buffer_size += size;
            ProcessCallback(requested_size, f);
        });
    }
}
