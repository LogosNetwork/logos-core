/// @file
/// This file contains implementation of the ConsensusNetIO and ConsensusNetIOManager classes, which handle
/// network connections between the delegates.
#include <logos/consensus/network/consensus_netio.hpp>
#include <logos/consensus/epoch_manager.hpp>
#include <logos/node/node.hpp>

const uint8_t ConsensusNetIO::CONNECT_RETRY_DELAY;

ConsensusNetIO::ConsensusNetIO(Service & service,
                               const Endpoint & endpoint,
                               logos::alarm & alarm,
                               const uint8_t remote_delegate_id, 
                               const uint8_t local_delegate_id, 
                               DelegateKeyStore & key_store,
                               MessageValidator & validator,
                               IOBinder iobinder,
                               std::recursive_mutex & connection_mutex,
                               EpochInfo & epoch_info,
                               NetIOErrorHandler & error_handler)
    : _socket(new Socket(service))
    , _connected(false)
    , _endpoint(endpoint)
    , _alarm(alarm)
    , _remote_delegate_id(remote_delegate_id)
    , _local_delegate_id(local_delegate_id)
    , _connections{0}
    , _key_store(key_store)
    , _validator(validator)
    , _io_channel_binder(iobinder)
    , _assembler(_socket, epoch_info, *this)
    , _connection_mutex(connection_mutex)
    , _epoch_info(epoch_info)
    , _error_handler(error_handler)
    , _last_timestamp(GetStamp())
{
    LOG_INFO(_log) << "ConsensusNetIO - Trying to connect to: "
                   <<  _endpoint << " remote delegate id "
                   << (int)remote_delegate_id
                   << " connection " << _epoch_info.GetConnectionName();

    Connect();
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
                               EpochInfo & epoch_info,
                               NetIOErrorHandler & error_handler)
    : _socket(socket)
    , _connected(false)
    , _endpoint(endpoint)
    , _alarm(alarm)
    , _remote_delegate_id(remote_delegate_id)
    , _local_delegate_id(local_delegate_id)
    , _connections{0}
    , _key_store(key_store)
    , _validator(validator)
    , _io_channel_binder(iobinder)
    , _assembler(_socket, epoch_info, *this)
    , _connection_mutex(connection_mutex)
    , _epoch_info(epoch_info)
    , _error_handler(error_handler)
    , _last_timestamp(GetStamp())
{
    LOG_INFO(_log) << "ConsensusNetIO client connected from: " << endpoint
                   << " remote delegate id " << (int)_remote_delegate_id
                   << " connection " << _epoch_info.GetConnectionName();
    OnConnect();
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

    std::lock_guard<std::mutex> lock(_send_mutex);

    if(!_sending)
    {
        _sending = true;

        boost::asio::async_write(*_socket,
                                 boost::asio::buffer(send_buffer->data(),
                                                     size),
                                 [this, send_buffer](const ErrorCode & ec, size_t size)
                                 { OnWrite(ec, size); });
    }
    else
    {
        _queued_writes.push_back(send_buffer);
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
            _alarm.add(std::chrono::seconds(CONNECT_RETRY_DELAY),
                       std::bind(&ConsensusNetIO::Connect, this));
        }

        return;
    }

    auto ids = std::make_shared<ConnectedClientIds>(_epoch_info.GetEpochNumber(),
            _local_delegate_id,
            _epoch_info.GetConnection(),
            _endpoint.address().to_string().c_str());
    //assert(_endpoint.address().to_string().size() == INET6_ADDRSTRLEN);
    std::vector<uint8_t> buf;
    ids->Serialize(buf);
    boost::asio::async_write(*_socket, boost::asio::buffer(buf.data(), buf.size()),
                             [this, ids](const ErrorCode &ec, size_t){
        if(ec)
        {
            LOG_ERROR(_log) << "ConsensusNetIO - Error writing connected client info " << ec.message();
            OnNetIOError(ec);
            return;
        }

        OnConnect();
    });
}

void
ConsensusNetIO::SendKeyAdvertisement()
{
    KeyAdvertisement advert;
    advert.public_key = _validator.GetPublicKey();

    std::vector<uint8_t> buf;
    advert.Serialize(buf);
    Send(buf.data(), buf.size());
}

void
ConsensusNetIO::ReadPrequel()
{
    _assembler.ReadPrequel(std::bind(&ConsensusNetIO::OnPrequal, this,
                                     std::placeholders::_1));
}

void
ConsensusNetIO::AsyncRead(size_t bytes,
                          ReadCallback callback)
{
    _assembler.ReadBytes(callback, bytes);
}

void
ConsensusNetIO::OnPrequal(const uint8_t * data)
{
    LOG_DEBUG(_log) << "#grep# ConsensusNetIO::OnPrequal"
            << " tid=" << std::this_thread::get_id()
            << " this " << (uint64_t)this
            << " data=" << (uint64_t)data;

    bool error = false;
    logos::bufferstream stream(data, MessagePrequelSize);
    Prequel msg_prequel(error, stream);
    if(error)
    {
        LOG_ERROR(_log) << "ConsensusNetIO::OnPrequal - Failed to deserialize.";
        return;
    }

    if(msg_prequel.payload_size > MAX_MSG_SIZE)
    {
        HandleMessageError("Wrong message size");
    }
    LOG_DEBUG(_log) << __func__ << " Peng"
            << " tid=" << std::this_thread::get_id()
            << " version=" << (uint)msg_prequel.version
            << " type=" << (uint)msg_prequel.type
            << " consensus=" << (uint)msg_prequel.consensus_type
            << " payload=" << msg_prequel.payload_size;

//    if(msg_prequel.payload_size == 0)
//    {
        std::vector<uint8_t> temp(data, data+MessagePrequelSize);
        LOG_DEBUG(_log) << __func__ << " Peng"
                << " tid=" << std::this_thread::get_id()
                << " print buf " << to_string(temp);
    //}

    _assembler.ReadBytes(std::bind(&ConsensusNetIO::OnData, this,
                                               std::placeholders::_1,
                                               msg_prequel.version,
                                               msg_prequel.type,
                                               msg_prequel.consensus_type,
                                               msg_prequel.payload_size),
            msg_prequel.payload_size);
}

void
ConsensusNetIO::OnData(const uint8_t * data,
        uint8_t version,
        MessageType message_type,
        ConsensusType consensus_type,
        uint32_t payload_size)
{
    LOG_DEBUG(_log) << "#grep# ConsensusNetIO::OnData"
            << " tid=" << std::this_thread::get_id()
            << " this " << (uint64_t)this
            << " data=" << (uint64_t)data;

    LOG_DEBUG(_log) << __func__ << " Peng"
            << " tid=" << std::this_thread::get_id()
            << " version=" << (int)version
            << " type=" << (int)message_type
            << " consensus=" << (int)consensus_type
            << " payload=" << payload_size;

    bool error = false;
    logos::bufferstream stream(data, payload_size);

    if (consensus_type == ConsensusType::Any)
    {
        if (message_type == MessageType::Heart_Beat)
        {
            HeartBeat hb(error, stream, version);
            if(error)
            {
                HandleMessageError("deserialize HeartBeat");
                return;
            }
            OnHeartBeat(hb);
            ReadPrequel();
        }
        else if (message_type == MessageType::Key_Advert)
        {
            KeyAdvertisement key_adv(error, stream, version);
            if(error)
            {
                HandleMessageError("deserialize KeyAdvertisement");
                return;
            }
            OnPublicKey(key_adv);
            ReadPrequel();
        }
        else
        {
            LOG_FATAL(_log) << "ConsensusNetIO - unexpected message type "
                        << (int)message_type << " for consensus Any";
            trace_and_halt(); //TODO Carl and Greg: halt or disconnect?
        }
    }
    else
    {
        auto idx = ConsensusTypeToIndex(consensus_type);

        if (!(idx >= 0 && idx < CONSENSUS_TYPE_COUNT) || _connections[idx] == 0)
        {
            LOG_FATAL(_log) << "ConsensusNetIO - _consensus_connections is NULL: "
                            << idx;
            trace_and_halt(); //TODO Carl and Greg: halt or disconnect?
        }

        switch (message_type) {
        case MessageType::Pre_Prepare:
        case MessageType::Prepare:
        case MessageType::Rejection:
        case MessageType::Post_Prepare:
        case MessageType::Commit:
        case MessageType::Post_Commit:
            if( ! _connections[idx]->OnMessageData(data,
                    version,
                    message_type,
                    consensus_type,
                    payload_size))
                HandleMessageError("Wrong consensus message");
            else
                ReadPrequel();
            break;
        default:
            HandleMessageError("Wrong message type");
            break;
        }
    }
}

void 
ConsensusNetIO::OnPublicKey(KeyAdvertisement & key_adv)
{
    _key_store.OnPublicKey(_remote_delegate_id, key_adv.public_key);

    std::lock_guard<std::recursive_mutex> lock(_connection_mutex);
    _io_channel_binder(shared_from_this(), _remote_delegate_id);
}

void
ConsensusNetIO::AddConsensusConnection(
    ConsensusType t, 
    std::shared_ptr<MessageParser> connection)
{
    LOG_INFO(_log) << "ConsensusNetIO - Added consensus connection "
                   << ConsensusToName(t)
                   << ' ' << ConsensusTypeToIndex(t)
                   << " local delegate " << uint64_t(_local_delegate_id)
                    << " remote delegate " << uint64_t(_remote_delegate_id)
                    << " global " << (int)DelegateIdentityManager::_global_delegate_idx
                    << " Connection " << _epoch_info.GetConnectionName();

    _connections[ConsensusTypeToIndex(t)] = connection;
}

void
ConsensusNetIO::OnWrite(const ErrorCode & error, size_t size)
{
    if (error)
    {
        if (_connected)
        {
            LOG_ERROR(_log) << "ConsensusConnection - Error on write to socket: "
                                   << error.message() << ". Remote endpoint: "
                            << _endpoint;
            OnNetIOError(error);
        }
        return;
    }

    std::lock_guard<std::mutex> lock(_send_mutex);

    auto begin = _queued_writes.begin();
    auto end = _queued_writes.begin();
    std::advance(end, _queue_reservation);

    _queued_writes.erase(begin, end);

    if((_queue_reservation = _queued_writes.size()))
    {
        std::vector<boost::asio::const_buffer> buffers;

        for(auto entry = _queued_writes.begin(); entry != _queued_writes.end(); ++entry)
        {
            buffers.push_back(boost::asio::const_buffer((*entry)->data(),
                                                        (*entry)->size()));
        }

        boost::asio::async_write(*_socket, buffers,
                                 std::bind(&ConsensusNetIO::OnWrite, this,
                                           std::placeholders::_1,
                                           std::placeholders::_2));
    }
    else
    {
        _sending = false;
    }
}

void
ConsensusNetIO::Close()
{
    std::lock_guard<std::recursive_mutex>    lock(_error_mutex);

    if (_socket != nullptr && _connected)
    {
        LOG_DEBUG(_log) << "ConsensusNetIO::Close closing socket, connection "
                        << _epoch_info.GetConnectionName() << ", delegate "
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
        std::vector<uint8_t> buf;
        heartbeat.Serialize(buf);
        LOG_DEBUG(_log) << __func__ << " Peng print buf " << to_string(buf);

//        {//Peng debug
//            usleep(rand() % 1000000);
//        }

        Send(buf.data(), buf.size());
    }
}
