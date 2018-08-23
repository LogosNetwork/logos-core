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
        , _consensus_connections{0}
        , _key_store(key_store)
        , _validator(validator)
        , _io_channel_binder(iobinder)
        , _connection_mutex(connection_mutex)
{
    BOOST_LOG(_log) << "ConsensusNetIO - Trying to connect to: " << 
        _endpoint << " remote delegate id " << (int)remote_delegate_id <<
        " from " << local_ip << ":" << _endpoint.port();

    _socket->open(boost::asio::ip::udp::v4());
    //boost::asio::socket_base::reuse_address option;
    //_socket->set_option(option);
    ErrorCode ec;
    _socket->bind(Endpoint(boost::asio::ip::make_address_v4(local_ip), _endpoint.port()), ec);
    if (ec)
    {
        BOOST_LOG(_log) << "ConsensusNetIO - failed to bind " << ec.message();
    }
    //OnConnect("");
    //Connect(local_ip);
    _alarm.add(std::chrono::seconds(CONNECT_RETRY_DELAY+10),
                   std::bind(&ConsensusNetIO::Connect, this, local_ip));
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
        , _consensus_connections{0}
        , _key_store(key_store)
        , _validator(validator)
        , _io_channel_binder(iobinder)
        , _connection_mutex(connection_mutex)
{
    OnConnect("");
}

void 
ConsensusNetIO::Connect(
    std::string local_ip)
{
    _socket->async_connect(_endpoint,
                           [this, local_ip](ErrorCode const & ec) { OnConnect(ec, local_ip); });
}

void 
ConsensusNetIO::Send(
    const void *data, 
    size_t size)
{
    // multiple threads can send at the same time
    // TODO - MUST IMPLMENT ASYNC_WRITE + QUEUE
    std::lock_guard<std::mutex> lock(_send_mutex);
    
    if (!_connected)
    {
        BOOST_LOG(_log) << "ConsensusNetIO - socket not connected yet";
        return;
    }

    BOOST_LOG(_log) << "ConsensusNetIO - sending " << size;

    boost::system::error_code ec;
    //boost::asio::write(*_socket, boost::asio::buffer(data, size), ec);
    uint sizeof_prequel = sizeof(Prequel);
    //sent = _socket->send_to(boost::asio::buffer(data, size), _endpoint, 0, ec);
    uint sent = _socket->send_to(boost::asio::buffer(data, sizeof_prequel), _endpoint, 0, ec);
    uint offset = sizeof_prequel;
    uint cnt = 0;
    while (offset != size)
    {
        cnt++;
        uint tosend = size - offset;
        tosend = (tosend > max_udp_size) ? max_udp_size : tosend;
        BOOST_LOG(_log) << "ConsensusConnection - sending " << tosend << " size " << size << " offset " << offset;
        sent = _socket->send_to(boost::asio::buffer(((uint8_t*)data) + offset, tosend), _endpoint, 0, ec);
        if (sent != tosend)
        {
            BOOST_LOG(_log) << "ConsensusConnection - Error send: " << sent << " " << tosend;
            return;
        }
        offset += tosend;
    }

    if(ec)
    {
        BOOST_LOG(_log) << "ConsensusConnection - Error on write to socket: "
                      << ec.message() << ". Remote endpoint: "
                      << _endpoint;
    }
    else
    {
        BOOST_LOG(_log) << "ConsensusConnection - sent: " << offset << " " << cnt << " to " << _endpoint;
    }
}

void 
ConsensusNetIO::OnConnect(
    std::string local_ip)
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

void 
ConsensusNetIO::OnConnect(
    ErrorCode const & ec, 
    std::string local_ip)
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
    BOOST_LOG(_log) << "ConsensusNetIO - receiving prequel from " << _endpoint;
    _socket->async_receive_from(boost::asio::buffer(_receive_buffer.data(),
                                                          sizeof(Prequel)),
                                                          _endpoint,
                            std::bind(&ConsensusNetIO::OnData, this,
                                      std::placeholders::_1,
                                      std::placeholders::_2));
}

void ConsensusNetIO::AsyncReadCb(uint8_t *data, uint size, uint offset, function<void(const ErrorCode&, size_t)> cb)
{
    uint torecv = ((size-offset) > max_udp_size) ? max_udp_size : size-offset;
    _socket->async_receive_from(boost::asio::buffer(data+offset, torecv), _endpoint, 
        [this, data, size, offset, cb, torecv](const ErrorCode &ec, size_t s)mutable->void
    {
        if (ec)
        {
            BOOST_LOG(_log) << "ConsensusNetIO::AsyncRead Error - failed: " << ec.message();
        }
        else if (s != torecv)
        {
            BOOST_LOG(_log) << "ConsensusNetIO::AsyncRead Error - received less : " << s << " " << torecv;
        }
        else
        {
            BOOST_LOG(_log) << "ConsensusNetIO::AsyncRead received: " << s << " size " << 
                size << " offset " << offset << " torecv " << torecv;
            offset += torecv;
            if (offset == size)
            {
                cb(ec, size);
            }
            else
            {
                AsyncReadCb(data, size, offset, cb);
            }
        }
    });

}

void 
ConsensusNetIO::AsyncRead(
    void *data_,
    size_t size_,
    std::function<void(const boost::system::error_code &, size_t)> cb)
{
    BOOST_LOG(_log) << "ConsensusNetIO - receiving from " << _endpoint << " size " << size_;
    AsyncReadCb((uint8_t*)data_, size_, 0, cb);
}

void 
ConsensusNetIO::OnData(
    const ErrorCode & ec, 
    size_t size)
{
    ConsensusType consensus_type (static_cast<ConsensusType> (_receive_buffer.data()[2]));
    MessageType message_type (static_cast<MessageType> (_receive_buffer.data()[1]));

    BOOST_LOG(_log) << "ConsensusNetIO - received " << size << " " << ConsensusToName(consensus_type) <<
    " " << MessageToName(message_type) << " from " << _endpoint;

    if (consensus_type == ConsensusType::Any)
    {
        if (message_type != MessageType::Key_Advert)
        {
            BOOST_LOG(_log) << "ConsensusNetIO - unexpected message type for consensus Any " << 
                               _receive_buffer.data()[2];
            return;
        }
        else
        {
            AsyncRead(_receive_buffer.data() + sizeof(Prequel),
                                            sizeof(KeyAdvertisement)-
                                            sizeof(Prequel)
                                            ,
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
    else
    {
        BOOST_LOG(_log) << "ConsensusNetIO - received public key from: " << (int)_remote_delegate_id;
    }

    auto msg (*reinterpret_cast<KeyAdvertisement*>(_receive_buffer.data()));
    _key_store.OnPublicKey(_remote_delegate_id, msg.public_key);
    
    // make sure shared pointer is initialized by ConsensusNetIOManager
    std::lock_guard<std::recursive_mutex> lock(_connection_mutex);
    _io_channel_binder(shared_from_this(), _remote_delegate_id);

    ReadPrequel();
}

void 
ConsensusNetIO::AddConsensusConnection(
    ConsensusType t, 
    std::shared_ptr<IConsensusConnection> consensus_connection)
{
    BOOST_LOG(_log) << "ConsensusNetIO - Added consensus connection " << ConsensusToName(t) << ' ' <<
            (int)(static_cast<uint8_t>(t)) << ' ' << (int)_remote_delegate_id;
    _consensus_connections[(int)static_cast<uint8_t>(t)] = consensus_connection;
}

void 
ConsensusNetIO::AdjustSocket()
{
    boost::asio::socket_base::receive_buffer_size receive_option(12108864);
    boost::asio::socket_base::send_buffer_size send_option(12108864);

    _socket->set_option(receive_option);
    _socket->set_option(send_option);
}