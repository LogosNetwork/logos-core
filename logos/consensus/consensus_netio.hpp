#pragma once

#include <logos/consensus/peer_manager.hpp>
#include <logos/consensus/peer_acceptor.hpp>
#include <logos/consensus/consensus_manager_config.hpp>
#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/messages/messages.hpp>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>

#include <functional>
#include <map>

class IConsensusConnection;
class IConsensusManager;
class MessageValidator;
class ConsensusNetIO;
struct DelegateIdentities;

using _Service   = boost::asio::io_service;
using _Endpoint  = boost::asio::ip::tcp::endpoint;
using _Socket    = boost::asio::ip::tcp::socket;
using _Log       = boost::log::sources::logger_mt;
using _Config    = ConsensusManagerConfig;
using _Address   = boost::asio::ip::address;
using _ErrorCode = boost::system::error_code;
using _Delegates = std::vector<_Config::Delegate>;
using _IOBinder  = function<void(std::shared_ptr<ConsensusNetIO>, uint8_t)>;
using _ConsensusManagers = std::map<ConsensusType, IConsensusManager&>;

namespace logos
{
    class alarm;
}

/// NetIO public interface, passed to ConsensusConnection
class IIOChannel {
public:
    IIOChannel() {}
    virtual ~IIOChannel() {}
    virtual void Send(const void *, size_t) = 0;
    virtual void AsyncRead(boost::asio::mutable_buffer buffer, std::function<void(boost::system::error_code const&, size_t)>) = 0;
    virtual void ReadPrequel() = 0;
};

/// Network connection to a delegate. There is one connection per delegate.
/// This class creates the connection to a delegate as the client. ConsensusNetIOManager accepts
/// connections to other delegates as the server. The type of connection is
/// based on the delegate ip ordering.
class ConsensusNetIO: public IIOChannel, public std::enable_shared_from_this<ConsensusNetIO> {
public:
    ConsensusNetIO(_Service & service,
                   _Endpoint endpoint, 
                   logos::alarm & alarm, 
                   const uint8_t remote_delegate_id, 
                   DelegateKeyStore & key_store,
                   MessageValidator & validator,
                   _IOBinder binder,
                   std::string local_ip,
                   std::recursive_mutex & connection_mutex);

    ConsensusNetIO(std::shared_ptr<_Socket> socket, 
                   _Endpoint endpoint, 
                   logos::alarm & alarm, 
                   const uint8_t remote_delegate_id, 
                   DelegateKeyStore & key_store,
                   MessageValidator & validator,
                   _IOBinder binder,
                   std::recursive_mutex & connection_mutex);

    virtual ~ConsensusNetIO() {}

    /// Send the provided data 
    virtual void Send(const void *, size_t) override;
    /// Send specific message type
    template<typename TYPE>
    void Send(const TYPE & data)
    {
        Send(reinterpret_cast<const void *>(&data), sizeof(data));
    }
    
    /// Adds ConsensusConnection of ConsensusType to netio consensus callbacks
    /// NetIO reads messages headers and calls respecive consensus type to process the message
    void AddConsensusConnection(ConsensusType t, std::shared_ptr<IConsensusConnection> consensus_connection);

    /// Read prequel header, dispatch the message to respective consensus type
    virtual void ReadPrequel() override;
    /// Read data from the network
    virtual void AsyncRead(boost::asio::mutable_buffer buffer, std::function<void(boost::system::error_code const &, size_t)>) override;

    /// Change socket read/write buffering options
    void AdjustSocket();

private:
    /// Async connect to the server
    void Connect(std::string local_ip);
    /// Connected call back
    void OnConnect(std::string local_ip);
    /// Connected call back with error code set
    void OnConnect(_ErrorCode const &, std::string local_ip);
    /// Call back for async read
    void OnData(_ErrorCode const & ec, size_t);
    /// Send public key to the connected delegate
    void SendKeyAdvertisement();
    /// Public key callback
    void OnPublicKey(_ErrorCode const & ec, size_t size);

    static constexpr uint8_t  CONNECT_RETRY_DELAY = 5;
    using ReceiveBuffer = std::array<uint8_t, sizeof(KeyAdvertisement)>;

    std::shared_ptr<_Socket>                _socket;
    ReceiveBuffer                           _receive_buffer;
    _Log                                    _log;
    _Endpoint                               _endpoint;
    logos::alarm &                           _alarm;
    bool                                    _connected;
    uint8_t                                 _remote_delegate_id;
    std::shared_ptr<IConsensusConnection>   _consensus_connections[static_cast<uint8_t>(NumberOfConsensus)]; 
    DelegateKeyStore &                      _key_store;
    MessageValidator &                      _validator;
    _IOBinder                               _io_channel_binder;
    std::recursive_mutex &                  _connection_mutex;
};

/// ConsensusNetIOManagare manages connections to delegates
/// Binds net connections to ConsensusConnection
class ConsensusNetIOManager : public PeerManager {
public:

   ConsensusNetIOManager(
        _ConsensusManagers consensus_managers,
        _Service & service, 
        logos::alarm & alarm, 
        const _Config & config,
        DelegateKeyStore & key_store,
        MessageValidator & validator); 

    ~ConsensusNetIOManager() 
    {
    }

    /// Server connection accepted call back
    void OnConnectionAccepted(const Endpoint& endpoint, std::shared_ptr<Socket>) override;

    /// Bind connected net connections to ConsensusConnection
    void BindIOChannel(std::shared_ptr<ConsensusNetIO> netio, uint8_t);

private:

    _Delegates                                      _delegates;
    _ConsensusManagers                              _consensus_managers;
    std::vector<std::shared_ptr<ConsensusNetIO>>    _connections;
    _Log                                            _log;
    logos::alarm &                                  _alarm;
    PeerAcceptor                                    _peer_acceptor;
    DelegateKeyStore &                              _key_store;
    MessageValidator &                              _validator;
    std::recursive_mutex                            _connection_mutex;
    std::recursive_mutex                            _bind_mutex;
    uint8_t                                         _delegate_id;
};
