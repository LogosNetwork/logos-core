/// @file
/// This file contains implementation of the ConsensusNetIO and ConsensusNetIOManager classes, which handle
/// network connections between the delegates.
#include <logos/consensus/consensus_netio.hpp>
#include <logos/node/node.hpp>

const uint8_t ConsensusNetIO::CONNECT_RETRY_DELAY;

ConsensusNetIO::ConsensusNetIO(Service & service,
                               const Endpoint & endpoint, 
                               logos::alarm & alarm,
                               const uint8_t remote_delegate_id, 
                               DelegateKeyStore & key_store,
                               MessageValidator & validator,
                               IOBinder iobinder,
                               std::string local_ip,
                               std::recursive_mutex & connection_mutex) 
    : _socket(new Socket(service))
    , _endpoint(endpoint)
    , _alarm(alarm)
    , _remote_delegate_id(remote_delegate_id)
    , _connections{0}
    , _key_store(key_store)
    , _validator(validator)
    , _io_channel_binder(iobinder)
    , _assembler(_socket)
    , _connection_mutex(connection_mutex)
{
    BOOST_LOG(_log) << "ConsensusNetIO - Trying to connect to: "
                    <<  _endpoint << " remote delegate id "
                    << (int)remote_delegate_id;

    Connect(local_ip);
}

ConsensusNetIO::ConsensusNetIO(std::shared_ptr<Socket> socket, 
                               const Endpoint & endpoint, 
                               logos::alarm & alarm,
                               const uint8_t remote_delegate_id, 
                               DelegateKeyStore & key_store,
                               MessageValidator & validator,
                               IOBinder iobinder,
                               std::recursive_mutex & connection_mutex) 
    : _socket(socket)
    , _endpoint(endpoint)
    , _alarm(alarm)
    , _connected(false)
    , _remote_delegate_id(remote_delegate_id)
    , _connections{0}
    , _key_store(key_store)
    , _validator(validator)
    , _io_channel_binder(iobinder)
    , _assembler(_socket)
    , _connection_mutex(connection_mutex)
{
    OnConnect("");
}

void
ConsensusNetIO::Connect(
    const std::string & local_ip)
{
    _socket->async_connect(_endpoint,
                           [this, local_ip](ErrorCode const & ec)
                           {OnConnect(ec, local_ip);});
}

void
ConsensusNetIO::Send(
    const void *data, 
    size_t size)
{
    // TODO - Make writes asynchronous
    std::lock_guard<std::mutex> lock(_send_mutex);
    
    if (!_connected)
    {
        BOOST_LOG(_log) << "ConsensusNetIO - socket not connected yet";
        return;
    }

    boost::system::error_code ec;
    boost::asio::write(*_socket, boost::asio::buffer(data, size), ec);

    if(ec)
    {
        BOOST_LOG(_log) << "ConsensusConnection - Error on write to socket: "
                      << ec.message() << ". Remote endpoint: "
                      << _endpoint;
    }
}

void
ConsensusNetIO::OnConnect(
    const std::string & local_ip)
{
    BOOST_LOG(_log) << "ConsensusConnection - Connected to "
                    << _endpoint << ". Remote delegate id: "
                    << uint64_t(_remote_delegate_id);

    _connected = true;

#ifdef MULTI_IP
    if (local_ip != "")
    {
        char buff[16];
        sprintf (buff, "%s", local_ip.c_str());
        BOOST_LOG(_log) << "ConsensusConnection - Sending my address " << buff << " to remote";
        Send((void*)buff, 16);
    }
 #endif

    SendKeyAdvertisement();
    ReadPrequel();
}

void 
ConsensusNetIO::OnConnect(
    ErrorCode const & ec, 
    const std::string & local_ip)
{
    if(ec)
    {
        BOOST_LOG(_log) << "ConsensusNetIO - Error connecting to "
                        << _endpoint << " : " << ec.message()
                        << " Retrying in " << int(CONNECT_RETRY_DELAY)
                        << " seconds.";

        _socket->close();

        _alarm.add(std::chrono::seconds(CONNECT_RETRY_DELAY),
                   std::bind(&ConsensusNetIO::Connect, this, local_ip));

        return;
    }

    OnConnect(local_ip);
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
            BOOST_LOG(_log) << "ConsensusNetIO - unexpected message type for consensus Any "
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
            BOOST_LOG(_log) << "ConsensusNetIO - _consensus_connections is NULL: "
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

    _key_store.OnPublicKey(_remote_delegate_id,
                           reinterpret_cast<KeyAdvertisement*>(
                               _receive_buffer.data())->public_key
                           );

    std::lock_guard<std::recursive_mutex> lock(_connection_mutex);
    _io_channel_binder(shared_from_this(), _remote_delegate_id);

    ReadPrequel();
}

void 
ConsensusNetIO::AddConsensusConnection(
    ConsensusType t, 
    std::shared_ptr<IConsensusConnection> connection)
{
    BOOST_LOG(_log) << "ConsensusNetIO - Added consensus connection " << ConsensusToName(t)
                    << ' ' << ConsensusTypeToIndex(t)
                    << ' ' << uint64_t(_remote_delegate_id);

    _connections[ConsensusTypeToIndex(t)] = connection;
}
