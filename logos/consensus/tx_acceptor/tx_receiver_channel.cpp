// @file
// TxReceiverChannel class implements TxChannel to receive transactions from standalone
// TxAcceptor and forward them to ConsensusManager
//

#include <logos/consensus/tx_acceptor/tx_receiver_channel.hpp>
#include <logos/consensus/tx_acceptor/tx_message_header.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/node/node.hpp>

const std::chrono::seconds TxReceiverChannel::CONNECT_RETRY_DELAY{5};
const TxReceiverChannel::Seconds TxReceiverChannel::TIMEOUT{15};
const uint16_t TxReceiverChannel::INACTIVITY{60000}; // milliseconds, 60 seconds

void
TxReceiverNetIOAssembler::OnError(const Error &error)
{
    _error_handler.ReConnect(true);
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
    , _inactivity_timer(service)
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
TxReceiverChannel::ReConnect(bool cancel)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (cancel)
    {
        _inactivity_timer.cancel();
    }
    _socket->close();
    _alarm.add(CONNECT_RETRY_DELAY, [this](){Connect();});
}

void
TxReceiverChannel::OnConnect(const Error & ec)
{
    if (ec)
    {
        LOG_WARN(_log) << "TxReceiverChannel::OnConnect error: " << ec.message();
        ReConnect(false);
        return;
    }

    ScheduleTimer(TIMEOUT);

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
            ReConnect(true);
            return;
        }

        if (header.payload_size == 0) // heartbeat
        {
            LOG_INFO(_log) << "TxReceiverChannel::AsyncReadHeader received heartbeat";
            _last_received = GetStamp();
            AsyncReadHeader();
        }
        else
        {
            LOG_INFO(_log) << "TxReceiverChannel::AsyncReadHeader received header, "
                           << " number of blocks " << header.mpf
                           << " payload " << header.payload_size;
            AsyncReadMessage(header);
        }
    }, TxMessageHeader::MESSAGE_SIZE);
}

void
TxReceiverChannel::AsyncReadMessage(const TxMessageHeader & header)
{
    using Request = RequestMessage<ConsensusType::BatchStateBlock>;
    auto payload_size = header.payload_size;
    _assembler.ReadBytes([this, payload_size, header](const uint8_t *data) mutable -> void {
        logos::bufferstream stream(data, payload_size);
        std::shared_ptr<Request> block = nullptr;
        std::vector<std::shared_ptr<Request>> blocks;
        auto nblocks = header.mpf;
        bool error = false;

        LOG_DEBUG(_log) << "TxReceiverChannel::AsyncReadMessage received payload size "
                        << payload_size << " number blocks " << nblocks;

        while (nblocks > 0)
        {
            auto block = std::make_shared<StateBlock>(error, stream);
            if (error) {
                LOG_ERROR(_log) << "TxReceiverChannel::AsyncReadMessage deserialize error, payload size "
                                << payload_size;
                ReConnect(true);
                return;
            }
            blocks.push_back(static_pointer_cast<Request>(block));
            nblocks--;
        }

        _last_received = GetStamp();

        LOG_DEBUG(_log) << "TxReceiverChannel::AsyncReadMessage sending "
                        << blocks.size() << " to consensus protocol";

        auto response = _receiver.OnSendRequest(blocks);

        for (auto r : response)
        {
            LOG_DEBUG(_log) << "TxRec)eiverChannel::AsyncReadMessage response "
                            << ProcessResultToString(r.first)
                            << " " << r.second.to_string();
        }

        AsyncReadHeader();
    }, payload_size);
}

void
TxReceiverChannel::ScheduleTimer(const Seconds & timeout)
{
    _inactivity_timer.expires_from_now(timeout);
    _inactivity_timer.async_wait(std::bind(&TxReceiverChannel::OnTimeout, this,
                                 std::placeholders::_1));
}

void
TxReceiverChannel::OnTimeout(const Error &error)
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

    if (GetStamp() - _last_received > INACTIVITY)
    {
        ReConnect(false);
    }
    else
    {
        ScheduleTimer(TIMEOUT);
    }
}
