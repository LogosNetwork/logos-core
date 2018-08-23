//===-- logos/consensus/consensus_netio.hpp - ConsensusNetIO class declaration -------*- C++ -*-===//
//
// Open source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the ConsensusNetIO classes, which provide
/// net io interface to the underlying socket
///
//===----------------------------------------------------------------------===//
#pragma once

#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/messages/messages.hpp>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/write.hpp>

#include <functional>
#include <map>

class IConsensusConnection;
class MessageValidator;

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
    virtual void AsyncRead(void*, size_t, std::function<void(const boost::system::error_code &, size_t)> cb) = 0;
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
    //! Aliases
    using Service   = boost::asio::io_service;
    using Endpoint  = boost::asio::ip::udp::endpoint;
    using Socket    = boost::asio::ip::udp::socket;
    using Log       = boost::log::sources::logger_mt;
    using Address   = boost::asio::ip::address;
    using ErrorCode = boost::system::error_code;
    using IOBinder  = function<void(std::shared_ptr<ConsensusNetIO>, uint8_t)>;
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
    ConsensusNetIO(Service & service,
                   const Endpoint & endpoint, 
                   logos::alarm & alarm, 
                   const uint8_t remote_delegate_id, 
                   DelegateKeyStore & key_store,
                   MessageValidator & validator,
                   IOBinder binder,
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
    ConsensusNetIO(std::shared_ptr<Socket> socket, 
                   const Endpoint & endpoint, 
                   logos::alarm & alarm, 
                   const uint8_t remote_delegate_id, 
                   DelegateKeyStore & key_store,
                   MessageValidator & validator,
                   IOBinder binder,
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
    virtual void AsyncRead(void *, size_t, std::function<void(boost::system::error_code const &, size_t)>) override;

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
    void OnConnect(ErrorCode const &ec, std::string local_ip);
    //! Call back for async read
    /*!
      \param ec error code
      \param size size of received data
    */
    void OnData(const ErrorCode & ec, size_t size);
    //! Send public key to the connected peer
    void SendKeyAdvertisement();
    //! Public key callback
    /*!
      \param ec error code
      \param size size of received data
    */
    void OnPublicKey(const ErrorCode & ec, size_t size);

    void AsyncReadCb(uint8_t *data, uint size, uint offset, function<void(const ErrorCode&, size_t)>);
    static const uint max_udp_size = 65000;

    static constexpr uint8_t  CONNECT_RETRY_DELAY = 5; //!< Retry connecting to the peer
    using ReceiveBuffer = std::array<uint8_t, sizeof(KeyAdvertisement)>; //!< Receive buffer size

    std::shared_ptr<Socket>                _socket; //!< Connected socket
    ReceiveBuffer                           _receive_buffer; //!< receive buffer
    Log                                     _log; //!< boost asio log
    Endpoint                                _endpoint; //!< remote peer endpoint
    logos::alarm &                          _alarm; //!< alarm reference
    bool                                    _connected; //!< connected flag
    uint8_t                                 _remote_delegate_id; //!< id of connected pper
    //! vector of consensus bound to the network connection
    std::shared_ptr<IConsensusConnection>   _consensus_connections[static_cast<uint8_t>(NumberOfConsensus)]; 
    DelegateKeyStore &                      _key_store; //!< Delegates public key store
    MessageValidator &                      _validator; //!< Validator/Signer of consensus messages
    IOBinder                                _io_channel_binder; //!< Network i/o to consensus binder
    std::recursive_mutex &                  _connection_mutex; //!< Consensus_connection access mutex
    std::mutex                              _send_mutex; //!< Protect concurrent writes
};