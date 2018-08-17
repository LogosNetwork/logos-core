//===-- logos/consensus/consensus_netio.hpp - ConsensusNetIO and ConsensusNetIOManager class declaration -------*- C++ -*-===//
//
// Open source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the ConsensusNetIO and ConsensusNetIOManager classes, which handle
/// network connections between the delegates
///
//===----------------------------------------------------------------------===//
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

//! Aliases
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

//! ConsensusNetIO public interface, passed to ConsensusConnection
class IIOChannel {
public:
    //! Class constructor
    IIOChannel() {}
    //! Class destractro
    virtual ~IIOChannel() {}
    //! Send data
    /*!
      Sends data to the connected peer
      \param data data to be send
      \param size of the data
    */
    virtual void Send(const void *data, size_t size) = 0;
    //! Asynchronous read 
    /*!
      Reads data from the connected peer
      \param buffer boost asio buffer to store the read data
      \param cb call back handler
    */
    virtual void AsyncRead(boost::asio::mutable_buffer buffer, std::function<void(boost::system::error_code const&, size_t)> cb) = 0;
    //! Read consensus message's prequel
    virtual void ReadPrequel() = 0;
};

//! ConsensusNetIO represent connection to a peer
/*!
  Network connection to a peer. There is one connection per peer.
  This class creates the connection to a peer as the client. ConsensusNetIOManager accepts
  connections to other peers as the server. The type of connection is
  based on the delegate's id ordering.
*/
class ConsensusNetIO: public IIOChannel, public std::enable_shared_from_this<ConsensusNetIO> {
public:
    //! Class constructor
    /*!
      This constructor is called by ConsensusNetIOManager to initiate client
      connection to the peer
      \param service reference to boost asio service
      \param endpoint reference to peer's address
      \param alarm reference to alarm
      \param remote_delegate_id id of connected delegate
      \param key_store delegates public key store
      \param validator validator/signer of consensus messages
      \param binder callback for binding netio interface to related consensus
      \param local_ip local ip of this node's delegate
      \param connection_mutex mutex to protect consensus connections
    */
    ConsensusNetIO(_Service & service,
                   const _Endpoint & endpoint, 
                   logos::alarm & alarm, 
                   const uint8_t remote_delegate_id, 
                   DelegateKeyStore & key_store,
                   MessageValidator & validator,
                   _IOBinder binder,
                   std::string local_ip,
                   std::recursive_mutex & connection_mutex);

    //! Class constructor
    /*!
      This constructor is called by ConsensusNetIOManager for accepted
      connection from a peer
      \param socket connected peer's socket
      \param endpoint reference to peer's address
      \param alarm reference to alarm
      \param remote_delegate_id id of connected delegate
      \param key_store delegates public key store
      \param validator validator/signer of consensus messages
      \param binder callback for binding netio interface to related consensus
      \param connection_mutex mutex to protect consensus connections
    */
    ConsensusNetIO(std::shared_ptr<_Socket> socket, 
                   const _Endpoint & endpoint, 
                   logos::alarm & alarm, 
                   const uint8_t remote_delegate_id, 
                   DelegateKeyStore & key_store,
                   MessageValidator & validator,
                   _IOBinder binder,
                   std::recursive_mutex & connection_mutex);

    //! Class destractor
    virtual ~ConsensusNetIO() {}

    //! Send data
    /*!
      Sends data to the connected peer
      \param data data to be send
      \param size of the data
    */
    virtual void Send(const void *data, size_t size) override;
    //! Send specific message type
    /*!
      Sends specific message to the connected peer
      \param data message to be send
    */
    template<typename TYPE>
    void Send(const TYPE & data)
    {
        Send(reinterpret_cast<const void *>(&data), sizeof(data));
    }
    
    //! Adds ConsensusConnection of ConsensusType to netio consensus callbacks
    /*!
      Adds specific consensus type to be serviced by this netio channel
      \param t consensus type
      \param consensus_connection specific consensus connection
    */
    void AddConsensusConnection(ConsensusType t, std::shared_ptr<IConsensusConnection> consensus_connection);

    //! Read prequel header, dispatch the message to respective consensus type
    virtual void ReadPrequel() override;
    //! Asynchronous read 
    /*!
      Reads data from the connected peer
      \param buffer boost asio buffer to store the read data
      \param cb call back handler
    */
    virtual void AsyncRead(boost::asio::mutable_buffer buffer, std::function<void(boost::system::error_code const &, size_t)>) override;

    //! Change socket read/write buffering options
    void AdjustSocket();

private:
    //! Async connect to the peer
    /*!
      Asynchronously connect to the peer
      \param local_ip send ip to the connected peer
    */
    void Connect(std::string local_ip);
    //! Connected call back
    /*!
      Async connect call back
      \param local_ip send ip to the connected peer
    */
    void OnConnect(std::string local_ip);
    //! Connected call back with error code set
    /*!
      Async connect call back
      \param ec error code
      \param local_ip send ip to the connected peer
    */
    void OnConnect(_ErrorCode const &ec, std::string local_ip);
    //! Call back for async read
    /*!
      \param ec error code
      \param size size of received data
    */
    void OnData(_ErrorCode const & ec, size_t size);
    //! Send public key to the connected peer
    void SendKeyAdvertisement();
    //! Public key callback
    /*!
      \param ec error code
      \param size size of received data
    */
    void OnPublicKey(_ErrorCode const & ec, size_t size);

    static constexpr uint8_t  CONNECT_RETRY_DELAY = 5; //!< Retry connecting to the peer
    using ReceiveBuffer = std::array<uint8_t, sizeof(KeyAdvertisement)>; //!< Receive buffer size

    std::shared_ptr<_Socket>                _socket; //!< Connected socket
    ReceiveBuffer                           _receive_buffer; //!< receive buffer
    _Log                                    _log; //!< boost asio log
    _Endpoint                               _endpoint; //!< remote peer endpoint
    logos::alarm &                           _alarm; //!< alarm reference
    bool                                    _connected; //!< connected flag
    uint8_t                                 _remote_delegate_id; //!< id of connected pper
    //! vector of consensus bound to the network connection
    std::shared_ptr<IConsensusConnection>   _consensus_connections[static_cast<uint8_t>(NumberOfConsensus)]; 
    DelegateKeyStore &                      _key_store; //!< Delegates public key store
    MessageValidator &                      _validator; //!< Validator/Signer of consensus messages
    _IOBinder                               _io_channel_binder; //!< Network i/o to consensus binder
    std::recursive_mutex &                  _connection_mutex; //!< Consensus_connection access mutex
    std::mutex                              _send_mutex; //!< Protect concurrent writes
};

//! ConsensusNetIOManagare manages connections to peers
/*!
  Creates ConsensusNetIO instances either as the client to connect to remote peers
  or as accepted connection
*/
class ConsensusNetIOManager : public PeerManager {
public:

    
    //! Class constractor
    /*!
      The constractor is called by node
      \param consensus_managers dictionary of consensus_manager
      \param service reference to boost asio service
      \param reference to alarm
      \param config reference to consensus manager configuration
      \param key_store delegates public key store
      \param validator validator/signer of consensus messages
    */
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

    //! Server connection accepted call back
    /*!
      Called by PeerAcceptor
      \param endpoint connected peer endpoint
      \param socket connected peed socket
    */
    void OnConnectionAccepted(const Endpoint& endpoint, std::shared_ptr<Socket>) override;

    //! Bind connected net connections to ConsensusConnection
    /*!
      Calls registered consensus managers to bind netio channel to consensus connection
      \param netio channel
      \param delegate_id connected delegate id
    */
    void BindIOChannel(std::shared_ptr<ConsensusNetIO> netio, uint8_t delegate_id);

private:

    _Delegates                                      _delegates; //!< List of all delegates
    _ConsensusManagers                              _consensus_managers; //!< Dictionary of registered consensus managers
    std::vector<std::shared_ptr<ConsensusNetIO>>    _connections; //!< NetIO connections
    _Log                                            _log; //!< boost asio log
    logos::alarm &                                  _alarm; //!< alarm
    PeerAcceptor                                    _peer_acceptor; //!< PeerAcceptor instance
    DelegateKeyStore &                              _key_store; //!< Delegates public key store
    MessageValidator &                              _validator; //!< Validator/Signer of consensus messages
    std::recursive_mutex                            _connection_mutex; //!< NetIO connections access mutex
    std::recursive_mutex                            _bind_mutex; //!< NetIO consensus connections mutes
    uint8_t                                         _delegate_id; //!< The delegate id
};
