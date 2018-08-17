//===-- logos/consensus/consensus_netio.hpp - ConsensusNetIO and ConsensusNetIOManager class implementation -------*- C++ -*-===//
//
// Open source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains implementation of the ConsensusNetIO and ConsensusNetIOManager classes, which handle
/// network connections between the delegates
///
//===----------------------------------------------------------------------===//
#include <logos/consensus/consensus_netio.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/node/node.hpp>

const uint8_t ConsensusNetIO::CONNECT_RETRY_DELAY;

ConsensusNetIO::ConsensusNetIO(_Service & service,
                               const _Endpoint & endpoint, 
                               logos::alarm & alarm,
                               const uint8_t remote_delegate_id, 
                               DelegateKeyStore & key_store,
                               MessageValidator & validator,
                               _IOBinder iobinder,
                               std::string local_ip,
                               std::recursive_mutex & connection_mutex) 
        : _socket(new _Socket(service))
        , _endpoint(endpoint)
        , _alarm(alarm)
        , _remote_delegate_id(remote_delegate_id)
        , _consensus_connections{0}
        , _key_store(key_store)
        , _validator(validator)
        , _io_channel_binder(iobinder)
        , _connection_mutex(connection_mutex)
{
    BOOST_LOG(_log) << "ConsensusNetIO - Trying to connect to: " << _endpoint << " remote delegate id " << (int)remote_delegate_id;
    Connect(local_ip);
}

ConsensusNetIO::ConsensusNetIO(std::shared_ptr<_Socket> socket, 
                               const _Endpoint & endpoint, 
                               logos::alarm & alarm,
                               const uint8_t remote_delegate_id, 
                               DelegateKeyStore & key_store,
                               MessageValidator & validator,
                               _IOBinder iobinder,
                               std::recursive_mutex & connection_mutex) 
        : _socket(socket)
        , _endpoint(endpoint)
        , _alarm(alarm)
        , _connected(false)
        , _remote_delegate_id(remote_delegate_id)
        , _consensus_connections{0}
        , _key_store(key_store)
        , _validator(validator)
        , _io_channel_binder(iobinder)
        , _connection_mutex(connection_mutex)
{
    OnConnect("");
}

void ConsensusNetIO::Connect(std::string local_ip)
{
    _socket->async_connect(_endpoint,
                           [this, local_ip](_ErrorCode const & ec) { OnConnect(ec, local_ip); });
}

void ConsensusNetIO::Send(const void *data, size_t size)
{
    // multiple threads can send at the same time
    // TODO - MUST IMPLMENT ASYNC_WRITE + QUEUE
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

void ConsensusNetIO::OnConnect(std::string local_ip)
{
    BOOST_LOG(_log) << "ConsensusConnection - Connected to "
                    << _endpoint << ". Remote delegate id: "
                    << uint64_t(_remote_delegate_id);

    _connected = true;

    AdjustSocket();
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

void ConsensusNetIO::OnConnect(_ErrorCode const & ec, std::string local_ip)
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

void ConsensusNetIO::SendKeyAdvertisement()
{
    KeyAdvertisement advert;
    advert.public_key = _validator.GetPublicKey();
    Send(advert);
}

void ConsensusNetIO::ReadPrequel()
{
    boost::asio::async_read(*_socket, boost::asio::buffer(_receive_buffer.data(),
                                                          sizeof(Prequel)),
                            std::bind(&ConsensusNetIO::OnData, this,
                                      std::placeholders::_1,
                                      std::placeholders::_2));
}

void ConsensusNetIO::AsyncRead(boost::asio::mutable_buffer buffer, std::function<void(boost::system::error_code const &, size_t)> cb)
{
    boost::asio::async_read(*_socket, buffer, cb);
}

void ConsensusNetIO::OnData(_ErrorCode const & ec, size_t size)
{
    ConsensusType consensus_type (static_cast<ConsensusType> (_receive_buffer.data()[2]));
    MessageType message_type (static_cast<MessageType> (_receive_buffer.data()[1]));

    if (consensus_type == ConsensusType::Any)
    {
        if (message_type != MessageType::Key_Advert)
        {
            BOOST_LOG(_log) << "ConsensusNetIO - unexpected message type for consensus Any " << _receive_buffer.data()[2];
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
    } else {
        int idx = (int)static_cast<uint8_t>(consensus_type);
        if (!(idx >= 0 && idx < NumberOfConsensus) || _consensus_connections[idx] == 0)
        {
            BOOST_LOG(_log) << "ConsensusNetIO - _consensus_connections is NULL: " << idx;
            return;
        }
        _consensus_connections[idx]->OnPrequel(ec, _receive_buffer.data(), size);
    }
}
 
void ConsensusNetIO::OnPublicKey(_ErrorCode const & ec, size_t size)
{
    if(ec)
    {
        BOOST_LOG(_log) << "ConsensusNetIO - Error receiving message: " << ec.message();
        return;
    }

    auto msg (*reinterpret_cast<KeyAdvertisement*>(_receive_buffer.data()));
    _key_store.OnPublicKey(_remote_delegate_id, msg.public_key);
    
    // make sure shared pointer is initialized by ConsensusNetIOManager
    std::lock_guard<std::recursive_mutex> lock(_connection_mutex);
    _io_channel_binder(shared_from_this(), _remote_delegate_id);

    ReadPrequel();
}

void ConsensusNetIO::AddConsensusConnection(ConsensusType t, std::shared_ptr<IConsensusConnection> consensus_connection)
{
    BOOST_LOG(_log) << "ConsensusNetIO - Added consensus connection " << ConsensusToName(t) << ' ' <<
            (int)(static_cast<uint8_t>(t)) << ' ' << (int)_remote_delegate_id;
    _consensus_connections[(int)static_cast<uint8_t>(t)] = consensus_connection;
}

void ConsensusNetIO::AdjustSocket()
{
    boost::asio::socket_base::receive_buffer_size receive_option(12108864);
    boost::asio::socket_base::send_buffer_size send_option(12108864);

    _socket->set_option(receive_option);
    _socket->set_option(send_option);
}

ConsensusNetIOManager::ConsensusNetIOManager(_ConsensusManagers consensus_managers,
                                             _Service & service, 
                                             logos::alarm & alarm, 
                                             const _Config & config,
                                             DelegateKeyStore & key_store,
                                             MessageValidator & validator) 
    : _delegates(config.delegates)
    , _consensus_managers(consensus_managers)
    , _alarm(alarm)
    , _peer_acceptor(service, _log, Endpoint(boost::asio::ip::make_address_v4(config.local_address), 
        config.peer_port), this)
    , _key_store(key_store)
    , _validator(validator)
    , _delegate_id(config.delegate_id)
{
    std::set<_Address> server_endpoints;

    auto local_endpoint(Endpoint(boost::asio::ip::make_address_v4(config.local_address),
                        config.peer_port));

    _key_store.OnPublicKey(_delegate_id, _validator.GetPublicKey());

    for(auto & delegate : _delegates)
    {
        auto endpoint = Endpoint(boost::asio::ip::make_address_v4(delegate.ip),
                                 local_endpoint.port());

        if(delegate.id == _delegate_id)
        {
            continue;
        }

        if(_delegate_id < delegate.id)
        {
            std::lock_guard<std::recursive_mutex> lock(_connection_mutex);
            _connections.push_back(std::make_shared<ConsensusNetIO>(service, endpoint, _alarm,
                                                                    delegate.id, _key_store, _validator,
                                                                    std::bind(&ConsensusNetIOManager::BindIOChannel, this, std::placeholders::_1, std::placeholders::_2), 
                                                                    config.local_address, _connection_mutex));
        }
        else
        {
            server_endpoints.insert(endpoint.address());
        }
    }

    if(server_endpoints.size())
    {
        _peer_acceptor.Start(server_endpoints);
    }
}

void ConsensusNetIOManager::OnConnectionAccepted(const _Endpoint& endpoint, std::shared_ptr<Socket> socket)
{
    auto entry = std::find_if(_delegates.begin(), _delegates.end(),
                              [&](const _Config::Delegate & delegate){
                                  return delegate.ip == endpoint.address().to_string();
                              });

    assert(entry != _delegates.end());

    std::lock_guard<std::recursive_mutex> lock(_connection_mutex);
    _connections.push_back(std::make_shared<ConsensusNetIO>(socket, endpoint, _alarm, entry->id, _key_store, _validator,
        std::bind(&ConsensusNetIOManager::BindIOChannel, this, std::placeholders::_1, std::placeholders::_2),
        _connection_mutex));
}

void ConsensusNetIOManager::BindIOChannel(std::shared_ptr<ConsensusNetIO> netio, uint8_t remote_delegate_id)
{
    std::lock_guard<std::recursive_mutex> lock(_bind_mutex);
    DelegateIdentities ids{_delegate_id, remote_delegate_id};
    for (auto it = _consensus_managers.begin(); it != _consensus_managers.end(); ++it)
    {
        auto consensus_connection = it->second.BindIOChannel(netio, ids);
        netio->AddConsensusConnection(it->first, consensus_connection);
    }
}
