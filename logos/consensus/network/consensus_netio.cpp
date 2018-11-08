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
                               EpochInfo & epoch_info)
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
    , _assembler(_socket, _connected, epoch_info)
    , _connection_mutex(connection_mutex)
    , _epoch_info(epoch_info)
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
                               EpochInfo & epoch_info)
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
    , _assembler(_socket, _connected, epoch_info)
    , _connection_mutex(connection_mutex)
    , _epoch_info(epoch_info)
{
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

        _alarm.add(std::chrono::seconds(CONNECT_RETRY_DELAY),
                   std::bind(&ConsensusNetIO::Connect, this));

        return;
    }

    auto ids = std::make_shared<ConnectedClientIds>();
    *ids = {_epoch_info.GetEpochNumber(), _local_delegate_id, _epoch_info.GetConnection()};
    boost::asio::async_write(*_socket, boost::asio::buffer(ids.get(), sizeof(ConnectedClientIds)),
                             [this, ids](const ErrorCode &ec, size_t){
        if(ec)
        {
            LOG_ERROR(_log) << "ConsensusNetIO - Error writing connected client info " << ec.message();
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
    advert.remote_delegate_id = _local_delegate_id;
    Send(advert);
}

void
ConsensusNetIO::ReadPrequel()
{
    _assembler.ReadPrequel(std::bind(&ConsensusNetIO::OnData, this,
                                     std::placeholders::_1));
}

void
ConsensusNetIO::AsyncRead(size_t bytes,
                          ReadCallback callback)
{
    _assembler.ReadBytes(callback, bytes);
}

void
ConsensusNetIO::OnData(const uint8_t * data)
{
    ConsensusType consensus_type (static_cast<ConsensusType> (data[2]));
    MessageType message_type (static_cast<MessageType> (data[1]));

    if (consensus_type == ConsensusType::Any)
    {
        if (message_type != MessageType::Key_Advert)
        {
            LOG_ERROR(_log) << "ConsensusNetIO - unexpected message type for consensus Any "
                            << data[2];
            return;
        }
        else
        {
            memcpy(_receive_buffer.data(), data, sizeof(Prequel));
            _assembler.ReadBytes(std::bind(&ConsensusNetIO::OnPublicKey, this,
                                           std::placeholders::_1),
                                 sizeof(KeyAdvertisement) -
                                 sizeof(Prequel));
        }
    }
    else
    {
        auto idx = ConsensusTypeToIndex(consensus_type);

        if (!(idx >= 0 && idx < CONSENSUS_TYPE_COUNT) || _connections[idx] == 0)
        {
            LOG_ERROR(_log) << "ConsensusNetIO - _consensus_connections is NULL: "
                            << idx;
            return;
        }

        _connections[idx]->OnPrequel(data);
    }
}

void 
ConsensusNetIO::OnPublicKey(const uint8_t * data)
{
    memcpy(_receive_buffer.data() + sizeof(Prequel), data,
           sizeof(KeyAdvertisement) - sizeof(Prequel));

    auto msg (*reinterpret_cast<KeyAdvertisement*>(_receive_buffer.data()));

    _key_store.OnPublicKey(_remote_delegate_id, msg.public_key);

    std::lock_guard<std::recursive_mutex> lock(_connection_mutex);
    _io_channel_binder(shared_from_this(), _remote_delegate_id);

    ReadPrequel();
}

void
ConsensusNetIO::AddConsensusConnection(
    ConsensusType t, 
    std::shared_ptr<PrequelParser> connection)
{
    LOG_INFO(_log) << "ConsensusNetIO - Added consensus connection "
                   << ConsensusToName(t)
                   << ' ' << ConsensusTypeToIndex(t)
                   << " local delegate " << uint64_t(_local_delegate_id)
                    << " remote delegate " << uint64_t(_remote_delegate_id)
                    << " global " << (int)NodeIdentityManager::_global_delegate_idx
                    << " Connection " << _epoch_info.GetConnectionName();

    _connections[ConsensusTypeToIndex(t)] = connection;
}

void
ConsensusNetIO::OnWrite(const ErrorCode & error, size_t size)
{
    if(error)
    {
        if (_connected)
        {
            LOG_ERROR(_log) << "ConsensusConnection - Error on write to socket: "
                            << error.message() << ". Remote endpoint: "
                            << _endpoint;
        }
        else
        {
            return;
        }
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
    if (_socket != nullptr)
    {
        BOOST_LOG(_log) << "ConsensusNetIO::Close closing socket, connection "
                        << _epoch_info.GetConnectionName() << ", delegate "
                        << (int)_local_delegate_id << ", remote delegate " << (int)_remote_delegate_id
                        << ", global " << (int)NodeIdentityManager::_global_delegate_idx;
        _connected = false;
        _socket->cancel();
        _socket->close();
    }
}