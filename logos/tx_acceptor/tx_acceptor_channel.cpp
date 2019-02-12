// @file
// This file declares TxAcceptorChannel which provides communication between standalone TxAcceptor
// and Delegate

#include <logos/tx_acceptor/tx_acceptor_channel.hpp>
#include <logos/tx_acceptor/tx_message_header.hpp>
#include <logos/node/node.hpp>

const TxAcceptorChannel::Seconds TxAcceptorChannel::TIMEOUT{15};
const uint16_t TxAcceptorChannel::INACTIVITY{40000}; // milliseconds, 40 seconds

TxAcceptorChannel::TxAcceptorChannel(Service &service, const std::string & ip, const uint16_t port)
    : _service(service)
    , _endpoint(Endpoint(boost::asio::ip::make_address_v4(ip), port))
    , _delegate(service, _endpoint, *this)
    , _inactivity_timer(service)
{
    _delegate.Start();
}

void
TxAcceptorChannel::OnConnectionAccepted(const Endpoint endpoint, shared_ptr<Socket> socket)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!Validate(endpoint, socket))
    {
        LOG_ERROR(_log) << "TxAcceptorChannel::OnConnectionAccepted failed to validate " << endpoint;
        socket->close();
        return;
    }

    Reset(socket);

    ScheduleTimer(TIMEOUT);
}

logos::process_return
TxAcceptorChannel::OnSendRequest(std::shared_ptr<Request> block, bool should_buffer)
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
    else
    {
        _last_sent = GetStamp();
    }

    LOG_INFO(_log) << "TxAcceptorChannel::OnSendRequest sent " << header.payload_size
                   << ProcessResultToString(result.code);

    return result;
}

TxChannel::Responses
TxAcceptorChannel::OnSendRequest(std::vector<std::shared_ptr<Request>> &blocks)
{
    logos::process_result result{logos::process_result::progress};

    auto buf{std::make_shared<vector<uint8_t>>()};
    TxMessageHeader header(0, blocks.size());
    {
        logos::vectorstream stream(*buf);
        header.Serialize(stream);
        for (auto block : blocks)
        {
            header.payload_size += block->Serialize(stream);
        }
    }
    HeaderStream header_stream(buf->data(), TxMessageHeader::MESSAGE_SIZE);
    header.Serialize(header_stream);

    if (!AsyncSend(buf))
    {
        result={logos::process_result::initializing};
    }

    return {{result,0}};
}

void
TxAcceptorChannel::OnError(const Error & error)
{
    std::lock_guard<std::mutex> lock(_mutex);
    LOG_ERROR(_log) << "TxAcceptorChannel::OnError " << error.message();
    _inactivity_timer.cancel();
    _socket->close();
}

void
TxAcceptorChannel::ScheduleTimer(const Seconds & timeout)
{
    _inactivity_timer.expires_from_now(timeout);
    _inactivity_timer.async_wait(std::bind(&TxAcceptorChannel::OnTimeout, this,
                                 std::placeholders::_1));
}

void
TxAcceptorChannel::OnTimeout(const Error &error)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if(error)
    {
        if (error == boost::asio::error::operation_aborted)
        {
            return;
        }
        LOG_INFO(_log) << "TxAcceptorChanel::OnTimeout error: "
                       << error.message();
    }

    if (GetStamp() - _last_sent > INACTIVITY)
    {
        auto buf{std::make_shared<vector<uint8_t>>()};
        TxMessageHeader header(0);
        header.Serialize(*buf);

        if (!AsyncSend(buf))
        {
            LOG_ERROR(_log) << "TxAcceptorChannel::OnTimeout failed to send heartbeat";
        }
        else
        {
            _last_sent = GetStamp();
        }
    }

    ScheduleTimer(TIMEOUT);
}