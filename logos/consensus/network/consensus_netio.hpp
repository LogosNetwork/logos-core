/// @file
/// This file contains the declaration of the ConsensusNetIO classes, which provide
/// an interface to the underlying socket.
#pragma once

#include <logos/consensus/network/net_io_assembler.hpp>
#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/log.hpp>

#include <boost/asio/io_service.hpp>

#include <functional>
#include <map>

class PrequelParser;
class MessageValidator;

namespace logos
{
    class alarm;
}

/// ConsensusNetIO public interface, passed to ConsensusConnection.
class IOChannel
{

protected:

    using ReadCallback =
            std::function<void(const uint8_t * data)>;

public:

    IOChannel() = default;

    virtual ~IOChannel() {}

    /// Send data.
    ///
    /// Sends data to the connected peer
    ///     @param data data to be send
    ///     @param size of the data
    virtual void Send(const void *data, size_t size) = 0;

    /// Asynchronous read .
    ///
    /// Reads data from the connected peer.
    ///     @param buffer boost asio buffer to store the read data
    ///     @param cb call back handler
    virtual void AsyncRead(size_t bytes,
                           ReadCallback callback) = 0;

    /// Read consensus message's prequel.
    virtual void ReadPrequel() = 0;
};

/// ConsensusNetIO represent connection to a peer.
///
/// Network connection to a peer. There is one connection per peer.
/// This class creates the connection to a peer as the client.
/// ConsensusNetIOManager accepts connections to other peers as
/// the server. The type of connection is based on the delegates'
/// id ordering.
class ConsensusNetIO: public IOChannel,
                      public std::enable_shared_from_this<ConsensusNetIO>
{

    using Service       = boost::asio::io_service;
    using Endpoint      = boost::asio::ip::tcp::endpoint;
    using Socket        = boost::asio::ip::tcp::socket;
    using Address       = boost::asio::ip::address;
    using ErrorCode     = boost::system::error_code;
    using IOBinder      = function<void(std::shared_ptr<ConsensusNetIO>, uint8_t)>;
    using ReceiveBuffer = std::array<uint8_t, sizeof(KeyAdvertisement)>;
    using Connections   = std::shared_ptr<PrequelParser> [CONSENSUS_TYPE_COUNT];
    using QueuedWrites  = std::list<std::shared_ptr<std::vector<uint8_t>>>;

public:

    /// Class constructor
    ///
    /// This constructor is called by ConsensusNetIOManager to initiate a client
    /// connection to a peer acting as a server.
    ///     @param service reference to boost asio service
    ///     @param endpoint reference to peer's address
    ///     @param alarm reference to alarm
    ///     @param remote_delegate_id id of connected delegate
    ///     @param key_store delegates' public key store
    ///     @param validator validator/signer of consensus messages
    ///     @param binder callback for binding netio interface to a consensus manager
    ///     @param local_ip local ip of this node's delegate
    ///     @param connection_mutex mutex to protect consensus connections
    ConsensusNetIO(Service & service,
                   const Endpoint & endpoint, 
                   logos::alarm & alarm, 
                   const uint8_t remote_delegate_id, 
                   const uint8_t local_delegate_id, 
                   DelegateKeyStore & key_store,
                   MessageValidator & validator,
                   IOBinder binder,
                   std::recursive_mutex & connection_mutex);

    /// Class constructor.
    ///
    /// This constructor is called by ConsensusNetIOManager when the remote
    /// peer being connected acts as the server.
    ///     @param socket connected peer's socket
    ///     @param endpoint reference to peer's address/port
    ///     @param alarm reference to alarm
    ///     @param remote_delegate_id id of connected delegate
    ///     @param key_store delegates' public key store
    ///     @param validator validator/signer of consensus messages
    ///     @param binder callback for binding netio interface to a consensus manager
    ///     @param connection_mutex mutex to protect consensus connections
    ConsensusNetIO(std::shared_ptr<Socket> socket, 
                   const Endpoint & endpoint, 
                   logos::alarm & alarm, 
                   const uint8_t remote_delegate_id, 
                   const uint8_t local_delegate_id, 
                   DelegateKeyStore & key_store,
                   MessageValidator & validator,
                   IOBinder binder,
                   std::recursive_mutex & connection_mutex);

    virtual ~ConsensusNetIO() {}

    /// Send data
    ///
    ///  Sends data to the connected peer
    ///  @param data data to be send
    ///  @param size of the data
    virtual void Send(const void *data, size_t size) override;

    /// Sends specific message to the connected peer.
    ///     @param data message to be send
    template<typename TYPE>
    void Send(const TYPE & data)
    {
        Send(reinterpret_cast<const void *>(&data), sizeof(data));
    }
    
    /// Adds ConsensusConnection of ConsensusType to netio callbacks.
    ///
    /// Adds specific consensus type to be serviced by this netio channel.
    ///     @param t consensus type
    ///     @param consensus_connection specific consensus connection
    void AddConsensusConnection(ConsensusType t,
                                std::shared_ptr<PrequelParser> connection);

    /// Read prequel header, dispatch the message
    /// to respective consensus type.
    virtual void ReadPrequel() override;

    /// Asynchronous read.
    ///
    /// Reads data from the connected peer.
    ///     @param bytes number of bytes to read
    ///     @param callback called upon reading data
    virtual void AsyncRead(size_t bytes,
                           ReadCallback callback) override;

    /// Change socket read/write buffering options
    void AdjustSocket();

private:

    /// Async connect to the peer.
    ///
    /// Asynchronously connect to the peer.
    void Connect();

    /// Connected call back.
    ///
    /// Async connect call back.
    void OnConnect();

    /// Connected call back with error code set.
    ///
    /// Async connect call back.
    ///  @param ec error code
    void OnConnect(ErrorCode const & ec);

    /// Call back for async read
    ///
    ///     @param data data received
    void OnData(const uint8_t * data);

    /// Send public key to the connected peer.
    void SendKeyAdvertisement();

    /// Public key callback.
    ///
    ///     @param ec error code
    ///     @param size size of received data
    void OnPublicKey(const uint8_t * data);

    void OnWrite(const ErrorCode & error, size_t size);

    static constexpr uint8_t CONNECT_RETRY_DELAY = 5;        ///< Reconnect delay in seconds.

    std::shared_ptr<Socket> _socket;             		///< Connected socket
    ReceiveBuffer           _receive_buffer;     		///< receive buffer
	QueuedWrites            _queued_writes;             ///< data waiting to get sent on the network
    Log                     _log;                		///< boost asio log
    Endpoint                _endpoint;           		///< remote peer endpoint
    logos::alarm &          _alarm;              		///< alarm reference
    uint8_t                 _remote_delegate_id; 		///< id of connected peer
    uint8_t                 _local_delegate_id;  		///< id of the local delegate
    Connections             _connections;       		///< vector of connections bound to the net i/o
    DelegateKeyStore &      _key_store;          		///< Delegates' public key store
    MessageValidator &      _validator;          		///< Validator/Signer of consensus messages
    IOBinder                _io_channel_binder;  		///< Network i/o to consensus binder
    NetIOAssembler          _assembler;          		///< Assembles messages from TCP buffer
    std::recursive_mutex &  _connection_mutex;   		///< _connections access mutex
    std::mutex              _send_mutex;         		///< Protect concurrent writes
	uint64_t                _queue_reservation = 0;     ///< How many queued entries are being sent?
    bool                    _connected         = false; ///< is the socket is connected?
    bool                    _sending           = false; ///< is an async write in progress?
};
