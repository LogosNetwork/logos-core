// @file
// TxReceiverChannel class implements TxChannel to receive transactions from standalone
// TxAcceptor and forward them to ConsensusManager
//

#include <logos/tx_acceptor/tx_receiver_channel.hpp>
#include <logos/tx_acceptor/tx_message_header.hpp>
#include <logos/consensus/consensus_container.hpp>
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
                                     std::shared_ptr<TxChannelExt> receiver,
                                     const Config &config)
    : _service(service)
    , _endpoint(Endpoint(boost::asio::ip::make_address_v4(ip), port))
    , _socket(std::make_shared<Socket>(service))
    , _alarm(alarm)
    , _receiver(receiver)
    , _assembler(std::make_shared<TxReceiverNetIOAssembler>(_socket, *this))
    , _inactivity_timer(service)
    , _config(config)
{
}

void
TxReceiverChannel::Connect()
{
    std::weak_ptr<TxReceiverChannel> this_w = Self<TxReceiverChannel>::shared_from_this();
   _socket->async_connect(_endpoint,
                           [this_w](const Error & ec) {
       auto this_s = GetSharedPtr(this_w, "TxReceiverChannel::Connect, object destroyed");
       if (!this_s)
       {
           return;
       }
       this_s->OnConnect(ec);
    });
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
    std::weak_ptr<TxReceiverChannel> this_w = Self<TxReceiverChannel>::shared_from_this();
    _alarm.add(CONNECT_RETRY_DELAY, [this_w](){
        auto this_s = GetSharedPtr(this_w, "TxReceiverChannel::ReConnect, object destroyed");
        if (!this_s)
        {
            return;
        }
        this_s->Connect();
    });
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
    // send handshake to tx-acceptor
    // current implementation only supports handshake sent from the delegate to tx-acceptor
    // tx-acceptor doesn't prove its identity to the delegate since presently it doesn't
    // have any keys
    std::weak_ptr<TxReceiverChannel> this_w = Self<TxReceiverChannel>::shared_from_this();
    _receiver->GetIdentityManager().TxAcceptorHandshake(_socket,
                                                        ConsensusContainer::GetCurEpochNumber(),
                                                        0, // delegate id is not required, delegate's bls key is in config
                                                        _config.tx_acceptor_config.acceptor_ip.c_str(),
                                                        _config.tx_acceptor_config.bin_port,
                                                        _config.tx_acceptor_config.json_port,
                                                        [this_w](bool result) {
        auto this_s = GetSharedPtr(this_w, "TxReceiverChannel::OnConnect, object destroyed");
        if (!this_s)
        {
            return;
        }
        if (result)
        {
            this_s->ScheduleTimer(TIMEOUT);

            this_s->AsyncReadHeader();
        }
        else
        {
            this_s->ReConnect(false);
        }
    });
}

void
TxReceiverChannel::AsyncReadHeader()
{
    std::weak_ptr<TxReceiverChannel> this_w = Self<TxReceiverChannel>::shared_from_this();
    _assembler->ReadBytes([this_w](const uint8_t *data) {
        auto this_s = GetSharedPtr(this_w, "TxReceiverChannel::AsyncReadHeader, object destroyed");
        if (!this_s)
        {
            return;
        }
        bool error = false;
        TxMessageHeader header(error, data, TxMessageHeader::MESSAGE_SIZE);
        if (error)
        {
            LOG_ERROR(this_s->_log) << "TxReceiverChannel::AsyncReadHeader header deserialize error";
            this_s->ReConnect(true);
            return;
        }

        if (header.payload_size == 0) // heartbeat
        {
            LOG_INFO(this_s->_log) << "TxReceiverChannel::AsyncReadHeader received heartbeat";
            this_s->_last_received = GetStamp();
            this_s->AsyncReadHeader();
        }
        else
        {
            LOG_INFO(this_s->_log) << "TxReceiverChannel::AsyncReadHeader received header, "
                           << " number of blocks " << header.mpf
                           << " payload " << header.payload_size;
            this_s->AsyncReadMessage(header);
        }
    }, TxMessageHeader::MESSAGE_SIZE);
}

void
TxReceiverChannel::AsyncReadMessage(const TxMessageHeader & header)
{
    using DM = DelegateMessage<ConsensusType::Request>;
    std::weak_ptr<TxReceiverChannel> this_w = Self<TxReceiverChannel>::shared_from_this();
    auto payload_size = header.payload_size;
    _assembler->ReadBytes([this_w, payload_size, header](const uint8_t *data) mutable -> void {
        auto this_s = GetSharedPtr(this_w, "TxReceiverChannel::AsyncReadMessage, object destroyed");
        if (!this_s)
        {
            return;
        }
        logos::bufferstream stream(data, payload_size);
        std::vector<std::shared_ptr<DM>> blocks;
        auto nblocks = header.mpf;
        bool error = false;

        LOG_DEBUG(this_s->_log) << "TxReceiverChannel::AsyncReadMessage received payload size "
                        << payload_size << " number blocks " << nblocks;

        while (nblocks > 0)
        {
            auto block = DeserializeRequest(error, stream);
            if (error)
            {
                LOG_ERROR(this_s->_log) << "TxReceiverChannel::AsyncReadMessage deserialize error, payload size "
                                << payload_size;
                this_s->ReConnect(true);
                return;
            }
            blocks.push_back(static_pointer_cast<DM>(block));
            nblocks--;
        }

        this_s->_last_received = GetStamp();

        LOG_DEBUG(this_s->_log) << "TxReceiverChannel::AsyncReadMessage sending "
                        << blocks.size() << " to consensus protocol";

        auto response = this_s->_receiver->OnSendRequest(blocks);

        for (auto r : response)
        {
            LOG_DEBUG(this_s->_log) << "TxRec)eiverChannel::AsyncReadMessage response "
                            << ProcessResultToString(r.first)
                            << " " << r.second.to_string();
        }

        this_s->AsyncReadHeader();
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
