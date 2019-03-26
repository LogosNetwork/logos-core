/// @file
/// This file contains implementation of the ConsensusNetIO and ConsensusNetIOManager classes, which handle
/// network connections between the delegates.
#include <logos/network/consensus_netio.hpp>
#include <logos/node/delegate_identity_manager.hpp>
#include <logos/consensus/epoch_manager.hpp>
#include <logos/node/node.hpp>
#include <logos/lib/trace.hpp>
#include <boost/system/error_code.hpp>

const uint8_t ConsensusNetIO::CONNECT_RETRY_DELAY;

void
ConsensusNetIOAssembler::OnError(const Error &error)
{
    auto info = _epoch_info.lock();
    if (!info)
    {
        return;
    }
    // cancelled at the end of epoch transition
    if (_netio.Connected() && !info->IsWaitingDisconnect()) {
        LOG_ERROR(_log) << "NetIOAssembler - Error receiving message: "
                        << error.message() << " global " << (int) DelegateIdentityManager::_global_delegate_idx
                        << " connection " << info->GetConnectionName()
                        << " delegate " << info->GetDelegateName()
                        << " state " << info->GetStateName();
        _netio.OnNetIOError(error);
    }
}

inline
void
ConsensusNetIOAssembler::OnRead()
{
    _netio.UpdateTimestamp();
}

ConsensusNetIO::ConsensusNetIO(Service & service,
                               const Endpoint & endpoint,
                               logos::alarm & alarm,
                               const uint8_t remote_delegate_id, 
                               const uint8_t local_delegate_id, 
                               DelegateKeyStore & key_store,
                               MessageValidator & validator,
                               IOBinder iobinder,
                               std::recursive_mutex & connection_mutex,
                               std::shared_ptr<EpochInfo> epoch_info,
                               NetIOErrorHandler & error_handler,
                               CreatedCb &cb)
    : NetIOSend(std::make_shared<Socket>(service))
    , ConsensusMsgSink(service)
    , _socket(*this)
    , _connected(false)
    , _endpoint(endpoint)
    , _alarm(alarm)
    , _remote_delegate_id(remote_delegate_id)
    , _local_delegate_id(local_delegate_id)
    , _connections{}
    , _key_store(key_store)
    , _validator(validator)
    , _io_channel_binder(iobinder)
    , _assembler(std::make_shared<ConsensusNetIOAssembler>(_socket, epoch_info, *this))
    , _connection_mutex(connection_mutex)
    , _epoch_info(epoch_info)
    , _error_handler(error_handler)
    , _last_timestamp(GetStamp())
    , _error_handled(false)
{
    auto info = _epoch_info.lock();
    assert(info);
    LOG_INFO(_log) << "ConsensusNetIO - Trying to connect to: "
                   <<  _endpoint << " remote delegate id "
                   << (int)remote_delegate_id
                   << " connection " << info->GetConnectionName();

    cb = &ConsensusNetIO::Connect;
}

ConsensusNetIO::ConsensusNetIO(std::shared_ptr<Socket> socket, 
                               const Endpoint endpoint,
                               logos::alarm & alarm,
                               const uint8_t remote_delegate_id, 
                               const uint8_t local_delegate_id, 
                               DelegateKeyStore & key_store,
                               MessageValidator & validator,
                               IOBinder iobinder,
                               std::recursive_mutex & connection_mutex,
                               std::shared_ptr<EpochInfo> epoch_info,
                               NetIOErrorHandler & error_handler,
                               CreatedCb &cb)
    : NetIOSend(socket)
    , ConsensusMsgSink(socket->get_io_service())
    , _socket(socket)
    , _connected(false)
    , _endpoint(endpoint)
    , _alarm(alarm)
    , _remote_delegate_id(remote_delegate_id)
    , _local_delegate_id(local_delegate_id)
    , _connections{}
    , _key_store(key_store)
    , _validator(validator)
    , _io_channel_binder(iobinder)
    , _assembler(std::make_shared<ConsensusNetIOAssembler>(_socket, epoch_info, *this))
    , _connection_mutex(connection_mutex)
    , _epoch_info(epoch_info)
    , _error_handler(error_handler)
    , _last_timestamp(GetStamp())
{
    auto info = _epoch_info.lock();
    assert(info);
    LOG_INFO(_log) << "ConsensusNetIO client connected from: " << endpoint
                   << " remote delegate id " << (int)_remote_delegate_id
                   << " connection " << info->GetConnectionName();
    cb = &ConsensusNetIO::OnConnect;
}

void
ConsensusNetIO::Connect()
{
    _socket->async_connect(_endpoint,
                           [this](ErrorCode const & ec) 
						   { OnConnect(ec); });
}

void
ConsensusNetIO::Send(
    const void *data, 
    size_t size)
{
    if (!_connected)
    {
        LOG_WARN(_log) << "ConsensusNetIO - socket not connected yet";
        return;
    }

    auto send_buffer(std::make_shared<std::vector<uint8_t>>(size, uint8_t(0)));
    std::memcpy(send_buffer->data(), data, size);

    if (!AsyncSend(send_buffer))
    {
        LOG_ERROR(_log) << "ConsensusNetIO::Send - AsyncSend to endpoint " << _endpoint << " failed";
    }
}

void 
ConsensusNetIO::OnConnect()
{
    LOG_INFO(_log) << "ConsensusNetIO - Connected to "
                   << _endpoint << ". Remote delegate id: "
                   << uint64_t(_remote_delegate_id);

    _connected = true;

    SendKeyAdvertisement();
    ReadPrequel();
}

void 
ConsensusNetIO::OnConnect(
    ErrorCode const & ec)
{
    auto info = _epoch_info.lock();
    if (!info)
    {
        return;
    }

    if(ec)
    {
        LOG_WARN(_log) << "ConsensusNetIO - Error connecting to "
                       << _endpoint << " : " << ec.message()
                       << " Retrying in " << int(CONNECT_RETRY_DELAY)
                       << " seconds.";

        _socket->close();

        std::lock_guard<std::recursive_mutex> lock(_error_mutex);
        if (!_error_handled)
        {
            std::weak_ptr<ConsensusNetIO> this_w = Self<ConsensusNetIO>::shared_from_this();
            _alarm.add(std::chrono::seconds(CONNECT_RETRY_DELAY), [this_w]() {
                auto this_s = GetSharedPtr(this_w, "ConsensusNetIO::OnConnect, object destroyed");
                if (!this_s)
                {
                    return;
                }
                this_s->Connect();
            });
        }

        return;
    }

    ConnectedClientIds ids(info->GetEpochNumber(),
            _local_delegate_id,
            info->GetConnection(),
            _endpoint.address().to_string().c_str());
    auto buf = std::make_shared<std::vector<uint8_t>>();
    ids.Serialize(*buf);
    std::weak_ptr<ConsensusNetIO> this_w = Self<ConsensusNetIO>::shared_from_this();
    boost::asio::async_write(*_socket, boost::asio::buffer(buf->data(), buf->size()),
                             [this_w, ids](const ErrorCode &ec, size_t){
        auto this_s = GetSharedPtr(this_w, "ConsensusNetIO::OnConnect, object destroyed");
        if (!this_s)
        {
            return;
        }
        if(ec)
        {
            LOG_ERROR(this_s->_log) << "ConsensusNetIO - Error writing connected client info " << ec.message();
            this_s->OnNetIOError(ec);
            return;
        }

        this_s->OnConnect();
    });
}

void
ConsensusNetIO::SendKeyAdvertisement()
{
    KeyAdvertisement advert;
    advert.public_key = _validator.GetPublicKey();
    Send(advert);
}

void
ConsensusNetIO::ReadPrequel()
{
    std::weak_ptr<ConsensusNetIO> this_w = Self<ConsensusNetIO>::shared_from_this();
    _assembler->ReadPrequel([this_w](const uint8_t *data) {
        auto this_s = GetSharedPtr(this_w, "ConsensusNetIO::ReadPrequel, object destroyed");
        if (!this_s)
        {
            return;
        }
        this_s->OnPrequel(data);
    });
}

void
ConsensusNetIO::AsyncRead(size_t bytes,
                          ReadCallback callback)
{
    _assembler->ReadBytes(callback, bytes);
}

void
ConsensusNetIO::OnPrequel(const uint8_t * data)
{
    bool error = false;
    logos::bufferstream stream(data, MessagePrequelSize);
    Prequel msg_prequel(error, stream);
    if(error)
    {
        LOG_ERROR(_log) << "ConsensusNetIO::OnPrequal - Failed to deserialize.";
        return;
    }

    LOG_TRACE(_log) << __func__
            << " tid=" << std::this_thread::get_id()
            << " version=" << (int)msg_prequel.version
            << " type=" << (int)msg_prequel.type
            << " consensus=" << (int)msg_prequel.consensus_type
            << " payload=" << msg_prequel.payload_size;

    if(msg_prequel.payload_size > MAX_MSG_SIZE)
    {
        HandleMessageError("Wrong message size");
    }

    if(msg_prequel.payload_size != 0)
    {
        std::weak_ptr<ConsensusNetIO> this_w = Self<ConsensusNetIO>::shared_from_this();
        _assembler->ReadBytes([this_w, msg_prequel](const uint8_t *data)
         {
             auto this_s = GetSharedPtr(this_w, "ConsensusNetIO::OnPrequel, object destroyed");
             if (!this_s)
             {
                 return;
             }
             this_s->OnData(data,
                            msg_prequel.version,
                            msg_prequel.type,
                            msg_prequel.consensus_type,
                            msg_prequel.payload_size);
         }, msg_prequel.payload_size);
    }else{
        ReadPrequel();
    }
}

void
ConsensusNetIO::OnData(const uint8_t * data,
        uint8_t version,
        MessageType message_type,
        ConsensusType consensus_type,
        uint32_t payload_size)
{
    LOG_TRACE(_log) << __func__
            << " tid=" << std::this_thread::get_id()
            << " version=" << (int)version
            << " type=" << (int)message_type
            << " consensus=" << (int)consensus_type
            << " payload=" << payload_size;

    bool error = false;
    logos::bufferstream stream(data, payload_size);

    LOG_DEBUG(_log) << "ConsensusNetIO - received message type " << MessageToName(message_type)
                    << " for consensus type " << ConsensusToName(consensus_type)
                    << " from " << _endpoint;

    if (consensus_type == ConsensusType::Any)
    {
        if (message_type == MessageType::Heart_Beat)
        {
            HeartBeat hb(error, stream, version);
            if(error)
            {
                HandleMessageError("Deserialize HeartBeat");
                return;
            }
            OnHeartBeat(hb);
        }
        else if (message_type == MessageType::Key_Advert)
        {
            KeyAdvertisement key_adv(error, stream, version);
            if(error)
            {
                HandleMessageError("Deserialize KeyAdvertisement");
                return;
            }
            OnPublicKey(key_adv);
        }
        else
        {
            HandleMessageError("Wrong message type for consensus Any");
        }
    }
    else
    {
        auto idx = ConsensusTypeToIndex(consensus_type);

        //three valid consensus types, RequestBlock, MicroBlock, Epoch
        //the largest valid idx to _connections[idx] is 2.
        if (idx >= (CONSENSUS_TYPE_COUNT))
        {
            HandleMessageError("Consensus type out of range");
        }

        // backup is already destroyed
        if(_connections[idx].use_count() == 0)
        {
            auto info = _epoch_info.lock();
            stringstream str;
            if (info)
            {
                str << info->GetDelegateName() << " " << info->GetStateName();
            } else{
                str << "";
            }

            LOG_DEBUG(_log) << "ConsensusNetIO - a backup delegate is NULL: " << idx
                            << " " << str.str();
            return;
        }

        switch (message_type) {
        case MessageType::Pre_Prepare:
        case MessageType::Prepare:
        case MessageType::Rejection:
        case MessageType::Post_Prepare:
        case MessageType::Commit:
        case MessageType::Post_Commit:
        {
#ifdef P2PTEST
            // simulate network receive failure
            struct stat sb;
            std::string path = "./DB/Consensus_" +
                               std::to_string((int) DelegateIdentityManager::_global_delegate_idx) +
                               "/recvoff";
            if (stat(path.c_str(), &sb) == 0 && (sb.st_mode & S_IFMT) == S_IFREG) {
                break;
            }
#endif
            if (!AddToConsensusQueue(data,
                                     version,
                                     message_type,
                                     consensus_type,
                                     payload_size))
                HandleMessageError("Wrong consensus message");
            break;
        }
        default:
            HandleMessageError("Wrong message type");
            break;
        }
    }
    ReadPrequel();
}

void 
ConsensusNetIO::OnPublicKey(KeyAdvertisement & key_adv)
{
    _key_store.OnPublicKey(_remote_delegate_id, key_adv.public_key);

    std::lock_guard<std::recursive_mutex> lock(_connection_mutex);
    _io_channel_binder(Self<ConsensusNetIO>::shared_from_this(), _remote_delegate_id);
}

void
ConsensusNetIO::AddConsensusConnection(
    ConsensusType t, 
    std::shared_ptr<MessageParser> connection)
{
    auto info = _epoch_info.lock();
    if (!info)
    {
        return;
    }
    LOG_INFO(_log) << "ConsensusNetIO - Added consensus connection "
                   << ConsensusToName(t)
                   << ' ' << ConsensusTypeToIndex(t)
                   << " local delegate " << uint64_t(_local_delegate_id)
                    << " remote delegate " << uint64_t(_remote_delegate_id)
                    << " global " << (int)DelegateIdentityManager::_global_delegate_idx
                    << " Connection " << info->GetConnectionName();

    _connections[ConsensusTypeToIndex(t)] = connection;
}

void
ConsensusNetIO::OnError(const ErrorCode &error)
{
    LOG_DEBUG(_log) << "ConsensusConnection - Error on write to socket, connected: " << _connected
                    << ", error message: " << error.message() << ". Remote endpoint: "
                    << _endpoint;
    if (_connected)
    {
        OnNetIOError(error);
    }
}

void
ConsensusNetIO::Close()
{
    std::lock_guard<std::recursive_mutex>    lock(_error_mutex);

    auto info = _epoch_info.lock();
    if (info && _socket != nullptr && _connected)
    {
        LOG_DEBUG(_log) << "ConsensusNetIO::Close closing socket, connection "
                        << info->GetConnectionName() << ", delegate "
                        << (int)_local_delegate_id << ", remote delegate " << (int)_remote_delegate_id
                        << ", global " << (int)DelegateIdentityManager::_global_delegate_idx
                        << " ptr " << (uint64_t)this;
        _connected = false;
        _socket->cancel();
        _socket->close();
    }
}

void
ConsensusNetIO::OnNetIOError(const ErrorCode &ec, bool reconnect)
{
    std::lock_guard<std::recursive_mutex>    lock(_error_mutex);

    if (!_error_handled)
    {
        _error_handled = true;
        _queued_writes.clear();
        _queue_reservation = 0;

        Close();

        _error_handler.OnNetIOError(ec, _remote_delegate_id, reconnect);
    }
}

void
ConsensusNetIO::OnHeartBeat(HeartBeat &heartbeat)
{
    LOG_DEBUG(_log) << "ConsensusNetIO::OnHeartBeat, received heartbeat from "
                    << (int)_remote_delegate_id << " is request " << (uint)heartbeat.is_request;

    if (heartbeat.is_request)
    {
        heartbeat.is_request = false;
        Send(heartbeat);
    }

    _direct_connect++;
}

void ConsensusNetIO::HandleMessageError(const char * operation)
{
    LOG_ERROR(_log) << "ConsensusNetIO HandleMessageError: " << operation;
    auto error(boost::system::errc::make_error_code(boost::system::errc::errc_t::io_error));
    OnNetIOError(error, true);
}

bool
ConsensusNetIO::AddToConsensusQueue(const uint8_t * data,
                                    uint8_t version,
                                    MessageType message_type,
                                    ConsensusType consensus_type,
                                    uint32_t payload_size,
                                    uint8_t delegate_id)
{
    return Push(data, version, message_type, consensus_type, payload_size, false);
}

void
ConsensusNetIO::OnMessage(std::shared_ptr<MessageBase> message,
                          MessageType message_type,
                          ConsensusType consensus_type,
                          bool is_p2p)
{
    auto idx = ConsensusTypeToIndex(consensus_type);
    auto backup_delegate = _connections[idx].lock();
    if (!backup_delegate)
    {
        LOG_DEBUG(_log) << "ConsensusNetIO::OnMessage, BackupDelegate<"
                        << ConsensusToName(consensus_type) << "> is destroyed";
        return;
    }

    backup_delegate->OnMessage(message, message_type, is_p2p);
}

template<template <ConsensusType> typename T>
std::shared_ptr<MessageBase>
ConsensusNetIO::make(ConsensusType consensus_type, logos::bufferstream &stream, uint8_t version)
{
    bool error = false;
    std::shared_ptr<MessageBase> msg;
    switch (consensus_type)
    {
        case ConsensusType::Request: {
            msg = std::make_shared<T<ConsensusType::Request>>(error, stream, version);
            break;
        }
        case ConsensusType::MicroBlock: {
            msg = std::make_shared<T<ConsensusType::MicroBlock>>(error, stream, version);
            break;
        }
        case ConsensusType::Epoch: {
            msg = std::make_shared<T<ConsensusType::Epoch>>(error, stream, version);
            break;
        }
        default: {
            LOG_ERROR(_log) << "ConsensusNetIO::Parser, invalid consensus type " << ConsensusToName(consensus_type);
            return nullptr;
        }
    }
    if (error)
    {
        LOG_ERROR(_log) << "ConsensusNetIO::Parser, failed to deserialize";
        msg = nullptr;
    }
    return msg;
}

std::shared_ptr<MessageBase>
ConsensusNetIO::Parse(const uint8_t * data, uint8_t version, MessageType message_type,
                      ConsensusType consensus_type, uint32_t payload_size)
{
    logos::bufferstream stream(data, payload_size);
    std::shared_ptr<MessageBase> msg = nullptr;

    switch (message_type)
    {
        case MessageType::Pre_Prepare: {
            msg = make<PrePrepareMessage>(consensus_type, stream, version);
            break;
        }
        case MessageType::Prepare: {
            msg = make<PrepareMessage>(consensus_type, stream, version);
            break;
        }
        case MessageType::Post_Prepare: {
            msg = make<PostPrepareMessage>(consensus_type, stream, version);
            break;
        }
        case MessageType::Commit: {
            msg = make<CommitMessage>(consensus_type, stream, version);
            break;
        }
        case MessageType::Post_Commit: {
            msg = make<PostCommitMessage>(consensus_type, stream, version);
            break;
        }
        case MessageType::Rejection: {
            msg = make<RejectionMessage>(consensus_type, stream, version);
            break;
        }
        default:
            return nullptr;
    }

    return msg;
}
