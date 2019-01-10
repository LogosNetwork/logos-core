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
#include <atomic>
#include <map>

class MessageParser;
class MessageValidator;
class EpochInfo;
class NetIOErrorHandler;

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

    virtual ~IOChannel() = default;

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

class IOChannelReconnect
{
protected:
    using ErrorCode     = boost::system::error_code;
public:
    IOChannelReconnect() = default;
    ~IOChannelReconnect() = default;
    virtual void OnNetIOError(const ErrorCode &ec, bool reconnect = true) = 0;
    virtual void UpdateTimestamp() = 0;
    virtual bool Connected() = 0;

};

/// ConsensusNetIO represent connection to a peer.
///
/// Network connection to a peer. There is one connection per peer.
/// This class creates the connection to a peer as the client.
/// ConsensusNetIOManager accepts connections to other peers as
/// the server. The type of connection is based on the delegates'
/// id ordering.
class ConsensusNetIO: public IOChannel,
                      public IOChannelReconnect,
                      public std::enable_shared_from_this<ConsensusNetIO>
{

    using Service       = boost::asio::io_service;
    using Endpoint      = boost::asio::ip::tcp::endpoint;
    using Socket        = boost::asio::ip::tcp::socket;
    using Address       = boost::asio::ip::address;
    using IOBinder      = function<void(std::shared_ptr<ConsensusNetIO>, uint8_t)>;
    using Connections   = std::shared_ptr<MessageParser> [CONSENSUS_TYPE_COUNT];
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
    ///     @param epoch_info epoch transition info
    ///     @param error_handler socket error handler
    ConsensusNetIO(Service & service,
                   const Endpoint & endpoint,
                   logos::alarm & alarm,
                   const uint8_t remote_delegate_id, 
                   const uint8_t local_delegate_id, 
                   DelegateKeyStore & key_store,
                   MessageValidator & validator,
                   IOBinder binder,
                   std::recursive_mutex & connection_mutex,
                   EpochInfo & epoch_info,
                   NetIOErrorHandler & error_handler);

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
    ///     @param epoch_info epoch transition info
    ///     @param error_handler socket error handler
    ConsensusNetIO(std::shared_ptr<Socket> socket,
                   const Endpoint endpoint,
                   logos::alarm & alarm,
                   const uint8_t remote_delegate_id, 
                   const uint8_t local_delegate_id, 
                   DelegateKeyStore & key_store,
                   MessageValidator & validator,
                   IOBinder binder,
                   std::recursive_mutex & connection_mutex,
                   EpochInfo & epoch_info,
                   NetIOErrorHandler & error_handler);

    virtual ~ConsensusNetIO()
    {
        LOG_DEBUG(_log) << "~ConsensusNetIO local delegate " << (int)_local_delegate_id
                        << " remote delegate " << (int)_remote_delegate_id
                        << " ptr " << (uint64_t)this;
    }

    /// Send data
    ///
    ///  Sends data to the connected peer
    ///  @param data data to be send
    ///  @param size of the data
    void Send(const void *data, size_t size) override;

    /// Sends specific message to the connected peer.
    ///     @param data message to be send
    template<typename TYPE>
    void Send(const TYPE & data)
    {
        std::vector<uint8_t> buf;
        data.Serialize(buf);
        Send(buf.data(), buf.size());
        //Send(reinterpret_cast<const void *>(&data), sizeof(data));
    }
    
    /// Adds ConsensusConnection of ConsensusType to netio callbacks.
    ///
    /// Adds specific consensus type to be serviced by this netio channel.
    ///     @param t consensus type
    ///     @param consensus_connection specific consensus connection
    void AddConsensusConnection(ConsensusType t,
                                std::shared_ptr<MessageParser> connection);

    /// Read prequel header, dispatch the message
    /// to respective consensus type.
    void ReadPrequel() override;

    /// Asynchronous read.
    ///
    /// Reads data from the connected peer.
    ///     @param bytes number of bytes to read
    ///     @param callback called upon reading data
    void AsyncRead(size_t bytes,
                   ReadCallback callback) override;


    void Close();

    /// Checks if delegate id is remote delegate id
    /// @param delegate_id delegate id
    /// @returns true if delegate id is remote delegate id
    bool IsRemoteDelegate(uint8_t delegate_id)
    {
        return _remote_delegate_id == delegate_id;
    }

    /// @return remote delegate id
    uint8_t GetRemoteDelegateId()
    {
        return _remote_delegate_id;
    }

    /// @return delegate's endpoint
    Endpoint & GetEndpoint()
    {
        return _endpoint;
    }

    /// @return true if connected
    bool Connected() override
    {
        return _connected;
    }

    /// @param ec error code
    /// @return true if error was already handled
    void OnNetIOError(const ErrorCode &ec, bool reconnect = true) override;

    /// Update timestamp of the last received message
    void UpdateTimestamp() override
    {
        _last_timestamp = GetStamp();
    }

    /// Get timestamp of the last received message
    /// @return timestamp
    uint64_t GetTimestamp()
    {
        return _last_timestamp;
    }

    /// Must be called right before destruction but
    /// not in the destructor
    void UnbindIOChannel()
    {
        for (uint8_t i = 0; i < CONSENSUS_TYPE_COUNT; ++i)
        {
            _connections[i].reset();
        }
    }

    static constexpr uint8_t CONNECT_RETRY_DELAY = 5;     ///< Reconnect delay in seconds.

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
    void OnData(const uint8_t * data,
            uint8_t version,
            MessageType message_type,
            ConsensusType consensus_type,
            uint32_t payload_size);

    void OnPrequel(const uint8_t * data);

    /// Send public key to the connected peer.
    void SendKeyAdvertisement();

    /// Public key callback.
    ///
    ///     @param data received data
    void OnPublicKey(KeyAdvertisement & key_adv);

    /// async_write callback
    /// @param error error code
    /// @param size size of written data
    void OnWrite(const ErrorCode & error, size_t size);

    /// Handle heartbeat message
    /// @param prequel data
    void OnHeartBeat(HeartBeat &hb);

    void HandleMessageError(const char * operation);

    std::shared_ptr<Socket>        _socket;               ///< Connected socket
    std::atomic_bool               _connected;            ///< is the socket is connected?
    QueuedWrites                   _queued_writes;        ///< data waiting to get sent on the network
    Log                            _log;                  ///< boost asio log
    Endpoint                       _endpoint;             ///< remote peer endpoint
    logos::alarm &                 _alarm;                ///< alarm reference
    uint8_t                        _remote_delegate_id;   ///< id of connected peer
    uint8_t                        _local_delegate_id;    ///< id of the local delegate
    Connections                    _connections;       	  ///< vector of connections bound to the net i/o
    DelegateKeyStore &             _key_store;            ///< Delegates' public key store
    MessageValidator &             _validator;            ///< Validator/Signer of consensus messages
    IOBinder                       _io_channel_binder;    ///< Network i/o to consensus binder
    NetIOAssembler                 _assembler;            ///< Assembles messages from TCP buffer
    std::recursive_mutex &         _connection_mutex;     ///< _connections access mutex
    std::mutex                     _send_mutex;           ///< Protect concurrent writes
	uint64_t                       _queue_reservation = 0;///< How many queued entries are being sent?
    bool                           _sending = false;      ///< is an async write in progress?
    EpochInfo &                    _epoch_info;           ///< Epoch transition info
    NetIOErrorHandler &            _error_handler;        ///< Pass socket error to ConsensusNetIOManager
    std::recursive_mutex           _error_mutex;          ///< Error handling mutex
    bool                           _error_handled;        ///< Socket error handled, prevent continous error loop
    std::atomic<uint64_t>          _last_timestamp;       ///< Last message timestamp
};
