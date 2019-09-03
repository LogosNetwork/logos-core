/// @file
/// This file contains implementation of the ConsensusNetIO and ConsensusNetIOManager classes, which handle
/// network connections between the delegates.
#include <logos/network/consensus_netio.hpp>
#include <logos/identity_management/delegate_identity_manager.hpp>
#include <logos/consensus/epoch_manager.hpp>
#include <logos/node/node.hpp>
#include <logos/lib/trace.hpp>
#include <boost/system/error_code.hpp>

const uint8_t ConsensusNetIO::CONNECT_RETRY_DELAY;

void
ConsensusNetIOAssembler::OnError(const Error &error)
{
    auto info = GetSharedPtr(_epoch_info, "ConsensusNetIOManager::OnError, object destroyed");
    if (!info)
    {
        return;
    }
    // cancelled at the end of epoch transition
    if (_netio.Connected() && !info->IsWaitingDisconnect()) {
        LOG_ERROR(_log) << "NetIOAssembler - Error receiving message: "
                        << error.message() << " global " << (int) DelegateIdentityManager::GetGlobalDelegateIdx()
                        << " connection " << info->GetConnectionName()
                        << " delegate " << info->GetDelegateName()
                        << " state " << info->GetStateName();
        _netio.OnNetIOError(error,true);
    }
}

inline
void
ConsensusNetIOAssembler::OnRead()
{
    _netio.UpdateTimestamp();
}

ConsensusNetIO::ConsensusNetIO(Service & service,
                               logos::alarm & alarm,
                               const uint8_t remote_delegate_id, 
                               const uint8_t local_delegate_id, 
                               IOBinder iobinder,
                               std::shared_ptr<EpochInfo> epoch_info,
                               ConsensusNetIOManager & netio_mgr,
                               bool is_server)//TODO change to is_server
    : ConsensusMsgSink(service)
    , _socket(is_server ? nullptr : std::make_shared<Socket>(service))
    , _connected(false)
    , _endpoint()
    , _alarm(alarm)
    , _remote_delegate_id(remote_delegate_id)
    , _local_delegate_id(local_delegate_id)
    , _connections{}
    , _io_channel_binder(iobinder)
    , _assembler(is_server ? nullptr : std::make_shared<ConsensusNetIOAssembler>(_socket, epoch_info, *this))
    , _io_send(is_server ? nullptr : std::make_shared<NetIOSend>(_socket))
    , _epoch_info(epoch_info)
    , _netio_mgr(netio_mgr)
    , _last_timestamp(GetStamp())
    , _connecting(false)
    , _epoch_over(false)
{
    auto info = GetSharedPtr(_epoch_info, "ConsensusNetIO::ConsensusNetIO, object destroyed");
    assert(info);
    LOG_INFO(_log) << "ConsensusNetIO::ConsensusNetIO -"
                   << "created for "
                   << (int)remote_delegate_id
                   << ",epoch =" << GetEpochNumber()
                   << ",is_server=" << is_server;
}

void ConsensusNetIO::MakeAndBindNewSocket()
{
    BindSocket(std::make_shared<Socket>(_netio_mgr.GetService()));
}

void ConsensusNetIO::BindSocket(
        std::shared_ptr<Socket> socket)
{
    std::lock_guard<std::recursive_mutex> lock(_connecting_mutex);
    auto epoch_info = GetSharedPtr(_epoch_info, "ConsensusNetIO::BindSocket, _epoch_info destroyed");
    if(epoch_info)
    {
        _socket = socket;
        _io_send = std::make_shared<NetIOSend>(_socket);
        _assembler = std::make_shared<ConsensusNetIOAssembler>(_socket, epoch_info, *this);
    }
    else
    {
        LOG_WARN(_log) << "ConsensusNetIO::BindSocket - failed to bind socket";
    }

}

void ConsensusNetIO::BindEndpoint(
        Endpoint endpoint)
{

    std::lock_guard<std::recursive_mutex> lock(_connecting_mutex);
    _endpoint = endpoint;
}

uint32_t ConsensusNetIO::GetEpochNumber()
{
    auto epoch_info = _epoch_info.lock();
    uint32_t epoch_number = 0;
    if(epoch_info)
    {
        epoch_number = epoch_info->GetEpochNumber();
    }
    return epoch_number;
}

void
ConsensusNetIO::Connect()
{

    std::lock_guard<std::recursive_mutex> lock(_connecting_mutex);

    _connecting = true;
    _connected = false;

    LOG_INFO(_log) << "ConsensusNetIO::Connect - attempting connection"
       << CommonInfoToLog();

    std::weak_ptr<ConsensusNetIO> this_w = 
        Self<ConsensusNetIO>::shared_from_this();

    auto this_s =
        GetSharedPtr(this_w, "ConsensusNetIO::Connect, object destroyed");

    _socket->async_connect(
            _endpoint,
            [this_s](ErrorCode const & ec)
            {
                // All callbacks should check if the epoch has ended before
                // proceeding.
                if(!this_s->CheckAndHandleEpochOver())
                {
                    this_s->OnConnect(ec);
                }
            });
}

void
ConsensusNetIO::Send(const void *data, size_t size)
{

    std::lock_guard<std::recursive_mutex> lock(_connecting_mutex);
    if (!_connected)
    {
        LOG_WARN(_log) << "ConsensusNetIO - socket not connected yet";
        return;
    }

    auto send_buffer(std::make_shared<std::vector<uint8_t>>(size, uint8_t(0)));
    std::memcpy(send_buffer->data(), data, size);

    LOG_INFO(_log) << "ConsensusNetIO::Send - "
        << CommonInfoToLog();

    if(_io_send)
    { 
        if (!_io_send->AsyncSend(send_buffer))
        {
            LOG_ERROR(_log) << "ConsensusNetIO::Send - AsyncSend to endpoint " << _endpoint << " failed";
        }
    }
    else
    {
        LOG_WARN(_log) << "ConsensusNetIO::Send - _io_send is null";
    }
}

void 
ConsensusNetIO::OnConnect()
{

    {
        std::lock_guard<std::recursive_mutex> lock(_connecting_mutex);

        LOG_INFO(_log) << "ConsensusNetIO::OnConnect - Connected"
            << CommonInfoToLog();

        UpdateTimestamp();
        _connected = true;
        _connecting = false;
        _direct_connect = 1;

        //epoch ended during a reconnect sequence, need to die now
        if(CheckAndHandleEpochOver())
        {
            return;
        }
    }

    _io_channel_binder(Self<ConsensusNetIO>::shared_from_this(), _remote_delegate_id);

    ReadPrequel();
}

void 
ConsensusNetIO::OnConnect(
    ErrorCode const & ec)
{

    std::lock_guard<std::recursive_mutex>    lock(_connecting_mutex);

    LOG_INFO(_log) << "ConsensusNetIO::OnConnect(ec)"
        << ", ec_msg = " << (ec ? ec.message() : "[empty]")
        << CommonInfoToLog();

    if(ec)
    {
        LOG_WARN(_log) << "ConsensusNetIO::OnConnect - Error connecting"
            << "ec.message = " << ec.message()
            << " Retrying in " << int(CONNECT_RETRY_DELAY) << " seconds."
            << CommonInfoToLog();

        _socket->close();

        std::weak_ptr<ConsensusNetIO> this_w =
            Self<ConsensusNetIO>::shared_from_this();

        auto this_s = GetSharedPtr(this_w,
                "ConsensusNetIO::OnConnect, object destroyed");

        _alarm.add(
                std::chrono::seconds(CONNECT_RETRY_DELAY),
                [this_s] ()
                {
                    if(!this_s->CheckAndHandleEpochOver())
                    {
                        this_s->Connect();
                    }
                }
        );
    }
    else
    {

        auto info = GetSharedPtr(_epoch_info,
                "ConsensusNetIO::OnConnect, info object destroyed");
        //TODO - when is this condition true?
        if(!info)
        {
            return;
        }

        std::weak_ptr<ConsensusNetIO> this_w =
            Self<ConsensusNetIO>::shared_from_this();
        auto this_s = GetSharedPtr(this_w,
                "ConsensusNetIO::OnConnect, this_s object destroyed");
        // should never happen
        if(!this_s)
        {
            LOG_FATAL(_log) << "ConsensusNetIO::OnConnect(ec)"
                << "-this_s is null."
                << CommonInfoToLog();
            trace_and_halt();
        }

        info->GetIdentityManager().ClientHandshake(
                _socket,
                info->GetEpochNumber(),
                _local_delegate_id,
                _remote_delegate_id,
                [this_s] (std::shared_ptr<AddressAd> ad)
                {
                    bool epoch_not_over = ! this_s->CheckAndHandleEpochOver();
                    if (ad)
                    {
                        if(ad->consensus_version != logos_version)
                        {
                            LOG_ERROR(this_s->_log) << "ConsensusNetIO::OnConnect, consensus version mismatch,"
                                            << " peer version=" << (int)ad->consensus_version
                                            << " my version=" << (int)logos_version;
                            if(epoch_not_over)
                            {
                                this_s->HandleMessageError("Client handshake");
                            }
                        } else {
                            LOG_INFO(this_s->_log)
                                << "ConsensusNetIO::OnConnect -"
                                << "client handshake was successful"
                                << this_s->CommonInfoToLog();

                            if(epoch_not_over)
                            {
                                this_s->OnConnect();
                            }
                        }
                    }
                    else
                    {
                        LOG_INFO(this_s->_log)
                            << "ConsensusNetIO::OnConnect -"
                            << "client handshake failed"
                            << this_s->CommonInfoToLog();

                        if(epoch_not_over)
                        {
                            this_s->HandleMessageError("Client handshake");
                        }
                    }
                }
        );
    }
}

void
ConsensusNetIO::ReadPrequel()
{

    LOG_INFO(_log) << "ConsensusNetIO::ReadPrequel - "
        << CommonInfoToLog();
    std::weak_ptr<ConsensusNetIO> this_w = Self<ConsensusNetIO>::shared_from_this();
    if(_assembler)
    {
        _assembler->ReadPrequel([this_w](const uint8_t *data) {
            auto this_s = GetSharedPtr(this_w, "ConsensusNetIO::ReadPrequel, object destroyed");
            if (!this_s)
            {
                return;
            }
            this_s->OnPrequel(data);
        });
    }
    else
    {
        LOG_WARN(_log) << "ConsensusNetIO::ReadPrequel - assembler is null";
    }
}

void
ConsensusNetIO::AsyncRead(size_t bytes,
                          ReadCallback callback)
{
    LOG_WARN(_log) << "ConsensusNetIO::AsyncRead - called";
    if(_assembler)
    {
        _assembler->ReadBytes(callback, bytes);
    }
}

void
ConsensusNetIO::OnPrequel(const uint8_t * data)
{
    LOG_INFO(_log) << "ConsensusNetIO::OnPrequel - "
        << CommonInfoToLog();
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
        if(_assembler)
        {
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
        }
        else
        {
            LOG_WARN(_log) << "ConsensusNetIO::OnPrequel - assembler is null";
        }
    }else{
        ReadPrequel();
    }
}

void
ConsensusNetIO::CheckHeartbeat()
{
    std::lock_guard<std::recursive_mutex> lock(_connecting_mutex);
    LOG_INFO(_log) << "ConsensusNetIO::CheckHeartbeat-"
        << CommonInfoToLog();

    // if we are not connected, do nothing
    if(!_connected) return;
    // if we are currently connecting, do nothing
    if(_connecting) return;

    using namespace boost::system::errc;
    HeartBeat heartbeat;

    auto stamp = GetTimestamp();
    auto now = GetStamp();
    auto diff = now - stamp;

    if (diff > ConsensusNetIOManager::MESSAGE_AGE_LIMIT)
    {

        LOG_DEBUG(_log) << "ConsensusNetIO::CheckHeartbeat"
            << "-timestamp is too old. attempting reconnect."
            << CommonInfoToLog();

        Error error(make_error_code(errc_t::io_error));
        OnNetIOError(error, true);
    }
    else if (diff > ConsensusNetIOManager::MESSAGE_AGE)
    {

        LOG_DEBUG(_log) << "ConsensusNetIO::CheckHeartbeat." 
            << "sending heartbeat."
            << CommonInfoToLog();

        std::vector<uint8_t> buf;
        heartbeat.Serialize(buf);
        Send(buf.data(), buf.size());
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

    LOG_DEBUG(_log) << "ConsensusNetIO::OnData - received message type " << MessageToName(message_type)
                    << " for consensus type " << ConsensusToName(consensus_type)
                    << " from " << _endpoint
                    << ", " << CommonInfoToLog();

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
            auto info = GetSharedPtr(_epoch_info, "ConsensusNetIO::OnData, object destroyed");
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
                               std::to_string((int) DelegateIdentityManager::GetGlobalDelegateIdx()) +
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
ConsensusNetIO::AddConsensusConnection(
    ConsensusType t, 
    std::shared_ptr<MessageParser> connection)
{
    auto info = GetSharedPtr(_epoch_info, "ConsensusNetIO::AddConsensusConnection, object destroyed");
    if (!info)
    {
        LOG_INFO(_log) << "ConsensusNetIO::AddConsensusConnection - "
            << " info is null"
            << CommonInfoToLog();
        return;
    }
    LOG_INFO(_log) << "ConsensusNetIO - Added consensus connection "
                   << ConsensusToName(t)
                   << ' ' << ConsensusTypeToIndex(t)
                    << " global " 
                    << (int)DelegateIdentityManager::GetGlobalDelegateIdx()
                    << " Connection " << info->GetConnectionName()
                    << CommonInfoToLog();


    _connections[ConsensusTypeToIndex(t)] = connection;
}

void
ConsensusNetIO::OnError(const ErrorCode &error)
{
    std::lock_guard<std::recursive_mutex>    lock(_connecting_mutex);
    LOG_DEBUG(_log) << "ConsensusConnection - Error on write to socket"
        << ", error message: " << error.message()
        << CommonInfoToLog();
    if (_connected)
    {
        OnNetIOError(error,true);
    }
}

void
ConsensusNetIO::Close()
{
    std::lock_guard<std::recursive_mutex>    lock(_connecting_mutex);

    if (_socket != nullptr && _connected)
    {
        LOG_DEBUG(_log) << "ConsensusNetIO::Close closing socket - "
            << CommonInfoToLog();
        _connected = false;
        _socket->close();
        _assembler.reset();
        _io_send.reset();
    }
}

void
ConsensusNetIO::OnNetIOError(const ErrorCode &ec, bool reconnect)
{
    std::lock_guard<std::recursive_mutex>    lock(_connecting_mutex);

    LOG_INFO(_log) << "ConsensusNetIO::OnNetIOError-"
        << CommonInfoToLog();

    //set flag to signal epoch has ended and this object needs to die
    //if a different thread is connecting, that thread will clean up
    if(!reconnect)
    {
        _epoch_over = true;
    }

    // If a different thread is currently connecting,
    // no need to initiate another reconnect sequence
    if (!_connecting)
    {
        LOG_INFO(_log) << "ConsensusNetIO::OnNetIOError-reconnecting"
            << CommonInfoToLog();

        //if the epoch is over, don't reconnect
        //(epoch_over could have been set to true by diff thread)
        if(_epoch_over)
        {
            reconnect = false;
            _connecting = false;
        }
        else
        {
            _connecting = true;
        }

        Close();
        std::weak_ptr<ConsensusNetIO> this_w =
            Self<ConsensusNetIO>::shared_from_this();
        auto this_s = GetSharedPtr(this_w,
                "ConsensusNetIOManager::OnNetIOError, object destroyed");

        //condition should never be true. Function is always called with
        //a shared ptr. All callbacks capture a shared ptr to this
        if(!this_s)
        {
            LOG_FATAL(_log) << "ConsensusNetIOManager::OnNetIOError -"
                << "this_s is destroyed"
                << CommonInfoToLog();
            trace_and_halt();
        }
        if(reconnect)
        {
            //Schedule the reconnect. shared ptr to this is captured
            //only connect if remote is the server
            if(_local_delegate_id < _remote_delegate_id)
            { 
                MakeAndBindNewSocket();
                LOG_DEBUG(_log) << "ConsensusNetIO::OnNetIOError-"
                    << "closing connection and attempting again."
                    << CommonInfoToLog();
                _alarm.add(Seconds(CONNECT_RETRY_DELAY),
                        [this_s]()
                        {
                            if(!this_s->CheckAndHandleEpochOver())
                            {
                                this_s->Connect();
                            }
                        
                        });
            }
            else
            {
                LOG_DEBUG(_log) << "ConsensusNetIO::OnNetIOError-"
                    << "Remote will reconnect-"
                    << CommonInfoToLog();
            
            }
            //reset direct connect count when this direct connection fails
            ResetConnectCount();

            if(!_netio_mgr.CanReachQuorumViaDirectConnect())
            {
                LOG_INFO(_log) << "ConsensusNetIO::OnNetIOError-reconnecting-"
                    << "enabling p2p"
                    << CommonInfoToLog();
                

                //enable p2p consensus if enough connections have failed to
                //prevent quorum
                _netio_mgr.EnableP2p(true);
            }
            else
            {
                LOG_INFO(_log) << "ConsensusNetIO::OnNetIOError-reconnecting-"
                    << "not enabling p2p"
                    << CommonInfoToLog();
            
            }
        }
    }
    else {
        LOG_INFO(_log) << "ConsensusNetIO::OnNetIOError-not reconnecting"
            << CommonInfoToLog();
    }
}

void
ConsensusNetIO::OnHeartBeat(HeartBeat &heartbeat)
{
    LOG_DEBUG(_log) << "ConsensusNetIO::OnHeartBeat, received heartbeat."
        << " is request " << (uint)heartbeat.is_request
        << CommonInfoToLog();

    if (heartbeat.is_request)
    {
        heartbeat.is_request = false;
        Send(heartbeat);
    }
    
    UpdateTimestamp();

    _direct_connect++;
}

void ConsensusNetIO::HandleMessageError(const char * operation, bool reconnect)
{
    std::lock_guard<std::recursive_mutex> lock(_connecting_mutex);

    LOG_ERROR(_log) << "ConsensusNetIO::HandleMessageError: " << operation
        << CommonInfoToLog();

    _connecting = false;

    using namespace boost::system::errc;
    Error error(make_error_code(errc_t::io_error));

    OnNetIOError(error, reconnect);
}

bool
ConsensusNetIO::CheckAndHandleEpochOver()
{
    std::lock_guard<std::recursive_mutex> lock(_connecting_mutex);
    if(_epoch_over)
    {
        LOG_INFO(_log) << "ConsensusNetIO::CheckAndHandleEpochOver -"
            << "epoch has ended"
            << CommonInfoToLog();
        using namespace boost::system::errc;
        Error error(make_error_code(errc_t::io_error));
        //Stop connecting
        _connecting = false;
        OnNetIOError(error, false);
        return true;
    }
    return false;
}

std::string ConsensusNetIO::CommonInfoToLog()
{
    std::string remote_del = "remote_delegate=" + std::to_string((int)_remote_delegate_id);
    std::string local_del = "local_delegate=" + std::to_string((int)_local_delegate_id);
    std::string epoch_num = "epoch_number=" + std::to_string(GetEpochNumber());
    std::string connected = "connected=" + std::to_string(_connected);
    std::string connecting = "connecting=" + std::to_string(_connecting);
    std::string endpoint = "endpoint=" + _endpoint.address().to_string();
    std::string epoch_over = "epoch_over=" + _epoch_over;
    std::string io_send = "io_send=" + std::string(_io_send? "not null" : "null");
    std::string assembler = "assembler=" + std::string(_assembler ? "not null" : "null");
    std::string res = 
        "-" + remote_del 
        + "," + local_del 
        + "," + epoch_num
        + "," + connected
        + "," + connecting
        + "," + endpoint
        + "," + epoch_over
        + "," + io_send
        + "," + assembler;
    return res;
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
    auto delegate_bridge = _connections[idx].lock();
    if (!delegate_bridge)
    {
        LOG_DEBUG(_log) << "ConsensusNetIO::OnMessage, BackupDelegate<"
                        << ConsensusToName(consensus_type) << "> is destroyed";
        return;
    }

    delegate_bridge->OnMessage(message, message_type, is_p2p);
}

template<template <ConsensusType> class T>
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
