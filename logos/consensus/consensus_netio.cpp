/// @file
/// This file contains implementation of the ConsensusNetIO and ConsensusNetIOManager classes, which handle
/// network connections between the delegates
///
//===----------------------------------------------------------------------===//
#include <logos/consensus/consensus_netio.hpp>
#include <logos/node/node.hpp>

const uint8_t ConsensusNetIO::CONNECT_RETRY_DELAY;
const size_t  ConsensusNetIO::SOCKET_BUFF_SIZE;

ConsensusNetIO::ConsensusNetIO(Service & service,
                               const Endpoint & endpoint, 
                               logos::alarm & alarm,
                               const uint8_t remote_delegate_id, 
                               const uint8_t local_delegate_id, 
                               DelegateKeyStore & key_store,
                               MessageValidator & validator,
                               IOBinder iobinder,
                               std::recursive_mutex & connection_mutex) 
        : _socket(new Socket(service))
        , _endpoint(endpoint)
        , _alarm(alarm)
        , _remote_delegate_id(remote_delegate_id)
        , _local_delegate_id(local_delegate_id)
        , _connections{0}
        , _key_store(key_store)
        , _validator(validator)
        , _io_channel_binder(iobinder)
        , _connection_mutex(connection_mutex)
{
    BOOST_LOG(_log) << "ConsensusNetIO - Trying to connect to: "
                    <<  _endpoint << " remote delegate id "
                    << (int)remote_delegate_id;

    Connect();
}

ConsensusNetIO::ConsensusNetIO(std::shared_ptr<Socket> socket, 
                               const Endpoint & endpoint, 
                               logos::alarm & alarm,
                               const uint8_t remote_delegate_id, 
                               const uint8_t local_delegate_id, 
                               DelegateKeyStore & key_store,
                               MessageValidator & validator,
                               IOBinder iobinder,
                               std::recursive_mutex & connection_mutex) 
        : _socket(socket)
        , _endpoint(endpoint)
        , _alarm(alarm)
        , _connected(false)
        , _remote_delegate_id(remote_delegate_id)
        , _local_delegate_id(local_delegate_id)
        , _connections{0}
        , _key_store(key_store)
        , _validator(validator)
        , _io_channel_binder(iobinder)
        , _connection_mutex(connection_mutex)
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
        BOOST_LOG(_log) << "ConsensusNetIO - Error on write to socket: "
                      << ec.message() << ". Remote endpoint: "
                      << _endpoint;
    }
}

void 
ConsensusNetIO::OnConnect()
{
    BOOST_LOG(_log) << "ConsensusNetIO - Connected to "
                    << _endpoint << ". Remote delegate id: "
                    << uint64_t(_remote_delegate_id);

    _connected = true;

    AdjustSocket();
    SendKeyAdvertisement();
    ReadPrequel();
}

void 
ConsensusNetIO::OnConnect(
    ErrorCode const & ec)
{
    if(ec)
    {
        BOOST_LOG(_log) << "ConsensusNetIO - Error connecting to "
                        << _endpoint << " : " << ec.message()
                        << " Retrying in " << int(CONNECT_RETRY_DELAY)
                        << " seconds.";

        _socket->close();

        _alarm.add(std::chrono::seconds(CONNECT_RETRY_DELAY),
                   std::bind(&ConsensusNetIO::Connect, this));

        return;
    }

    OnConnect();
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
    boost::asio::async_read(*_socket, boost::asio::buffer(_receive_buffer.data(),
                                                          sizeof(Prequel)),
                            std::bind(&ConsensusNetIO::OnData, this,
                                      std::placeholders::_1,
                                      std::placeholders::_2));
}

void 
ConsensusNetIO::AsyncRead(
    boost::asio::mutable_buffer buffer, 
    std::function<void(boost::system::error_code const &, size_t)> cb)
{
    boost::asio::async_read(*_socket, buffer, cb);
}

void 
ConsensusNetIO::OnData(
    ErrorCode const & ec, 
    size_t size)
{
    ConsensusType consensus_type (static_cast<ConsensusType> (_receive_buffer.data()[2]));
    MessageType message_type (static_cast<MessageType> (_receive_buffer.data()[1]));

    if (consensus_type == ConsensusType::Any)
    {
        if (message_type != MessageType::Key_Advert)
        {
            BOOST_LOG(_log) << "ConsensusNetIO - unexpected message type for consensus Any "
                            << _receive_buffer.data()[2];
            return;
        }
        else
        {
            AsyncRead(boost::asio::buffer(_receive_buffer.data() + sizeof(Prequel),
                                            sizeof(KeyAdvertisement)-
                                            sizeof(Prequel)
                                            ),
                                            std::bind(&ConsensusNetIO::OnPublicKey, this,
                                                        std::placeholders::_1,
                                                        std::placeholders::_2));
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

        _connections[idx]->OnPrequel(ec, _receive_buffer.data(), size);
    }
}
 
void 
ConsensusNetIO::OnPublicKey(
    ErrorCode const & ec, 
    size_t size)
{
    if(ec)
    {
        BOOST_LOG(_log) << "ConsensusNetIO - Error receiving message: " << ec.message();
        return;
    }

    auto msg (*reinterpret_cast<KeyAdvertisement*>(_receive_buffer.data()));

    // update remote delegate id
    _remote_delegate_id = msg.remote_delegate_id;

    _key_store.OnPublicKey(_remote_delegate_id, msg.public_key);
    
    std::lock_guard<std::recursive_mutex> lock(_connection_mutex);
    _io_channel_binder(shared_from_this(), _remote_delegate_id);

    ReadPrequel();
}

void 
ConsensusNetIO::AddConsensusConnection(
    ConsensusType t, 
    std::shared_ptr<IConsensusConnection> consensus_connection)
{
    BOOST_LOG(_log) << "ConsensusNetIO - Added consensus connection " << ConsensusToName(t)
                    << ' ' << ConsensusTypeToIndex(t)
                    << ' ' << uint64_t(_remote_delegate_id);

    _connections[ConsensusTypeToIndex(t)] = consensus_connection;
}

void 
ConsensusNetIO::AdjustSocket()
{
    boost::asio::socket_base::receive_buffer_size receive_option(SOCKET_BUFF_SIZE);
    boost::asio::socket_base::send_buffer_size send_option(SOCKET_BUFF_SIZE);

    _socket->set_option(receive_option);
    _socket->set_option(send_option);
}