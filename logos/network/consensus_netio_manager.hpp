/// @file
/// This file contains the declaration of the ConsensusNetIOManager classes, which handles
/// network connections between the delegates
#pragma once

#include <logos/consensus/consensus_manager_config.hpp>
#include <logos/network/consensus_netio.hpp>
#include <logos/network/peer_acceptor.hpp>
#include <logos/lib/utility.hpp>

class NetIOHandler;
class PeerAcceptorStarter;
class EpochInfo;

class NetIOErrorHandler
{
protected:
    using Error = boost::system::error_code;
public:
    NetIOErrorHandler() = default;
    ~NetIOErrorHandler() = default;
    virtual void EnableP2p(bool enable) = 0;
    virtual bool CanReachQuorumViaDirectConnect() = 0;
};

/// ConsensusNetIOManager manages connections to peers.
///
/// Creates ConsensusNetIO instances either as the client to connect
/// to remote peers or as an accepted connection.
class ConsensusNetIOManager : public NetIOErrorHandler,
                              public ConsensusMsgProducer,
                              public Self<ConsensusNetIOManager>
{

    using Service     = boost::asio::io_service;
    using Endpoint    = boost::asio::ip::tcp::endpoint;
    using Socket      = boost::asio::ip::tcp::socket;
    using Config      = ConsensusManagerConfig;
    using Address     = boost::asio::ip::address;
    using Delegates   = std::unordered_map<uint8_t,Config::Delegate>;
    using Managers    = std::map<ConsensusType, std::shared_ptr<NetIOHandler>>;
    using Connections = std::vector<std::shared_ptr<ConsensusNetIO>>;
    using Timer       = boost::asio::deadline_timer;

public:

    /// Class constructor.
    ///
    /// The constructor is called by node.
    ///     @param consensus_managers maps ConsensusType to ConsensusManager
    ///     @param service reference to boost asio service
    ///     @param reference to alarm
    ///     @param config reference to consensus manager configuration
    ///     @param starter starts accepting peer connections
    ///     @param epoch_info epoch info
    ConsensusNetIOManager(
        std::shared_ptr<NetIOHandler> request_manager,
        std::shared_ptr<NetIOHandler> micro_manager,
        std::shared_ptr<NetIOHandler> epoch_manager,
        Service & service,
        logos::alarm & alarm, 
        const Config & config,
        PeerAcceptorStarter & starter);

    ~ConsensusNetIOManager();

    /// Server connection accepted call back.
    ///
    /// Called by PeerAcceptor.
    ///     @param endpoint connected peer endpoint
    ///     @param socket connected peer socket
    ///     @param delegate_id peer's delegate id
    void OnConnectionAccepted(const Endpoint endpoint,
                              std::shared_ptr<Socket>,
                              uint8_t delegate_id);

    /// Bind connected IO Channel to ConsensusConnection.
    ///
    /// Calls registered consensus managers to bind netio
    /// channel to consensus connection.
    ///     @param netio channel
    ///     @param delegate_id connected delegate id
    void BindIOChannel(std::shared_ptr<ConsensusNetIO> netio,
                       uint8_t delegate_id);

    /// Cleaup up before destruction
    void CleanUp();

    bool AddToConsensusQueue(const uint8_t * data,
                             uint8_t version,
                             MessageType message_type,
                             ConsensusType consensus_type,
                             uint32_t payload_size,
                             uint8_t delegate_id=0xff) override;

    void Start(std::shared_ptr<EpochInfo> epoch_info);

    void AddDelegate(uint8_t delegate_id, std::string &ip, uint16_t port);
    static const uint64_t MESSAGE_AGE;
    static const uint64_t MESSAGE_AGE_LIMIT;

    Service& GetService()
    {
        return _service;
    }

    /// Enable p2p on all consensus managers
    void EnableP2p(bool enable) override;

    /// @returns true if we have enough direct connections to reach quorum
    bool CanReachQuorumViaDirectConnect() override;


protected:


    /// Create netio instance and add to connecitons
    /// @param remote_delegate_id remote delegate id
    /// @returns ConsensusNetIO object that does not yet have a connection
    /// Actual connection is bound later when learning the peers endpoint
    /// or when the peer connects to us
    std::shared_ptr<ConsensusNetIO> AddNetIOConnection(uint8_t remote_delegate_id);

    /// Schedule heartbeat/garbage colleciton timer
    /// @param seconds timer's timeout value
    void ScheduleTimer(boost::posix_time::seconds);

    /// Timer's timeout callback
    /// @param error error code
    void OnTimeout(const Error &error);


    uint32_t GetEpochNumber();



private:
    static const boost::posix_time::seconds HEARTBEAT;


    Service &                      _service;            ///< Boost asio service reference
    Delegates                      _delegates;          ///< List of all delegates
    Managers                       _consensus_managers; ///< Dictionary of registered consensus managers
    Connections                    _connections;        ///< NetIO connections
    Log                            _log;                ///< boost asio log
    logos::alarm &                 _alarm;              ///< alarm
    std::recursive_mutex           _connection_mutex;   ///< NetIO connections access mutex
    std::recursive_mutex           _bind_mutex;         ///< NetIO consensus connections mutex
    uint8_t                        _delegate_id;        ///< The local delegate id
    std::weak_ptr<EpochInfo>       _epoch_info;         ///< Epoch transition info
    Timer                          _heartbeat_timer;    ///< Heartbeat/gb timer to handle heartbeat and gb
    std::mutex                     _gb_mutex;           ///< Garbage mutex
    Config                         _config;
    PeerAcceptorStarter &          _acceptor;
    Timer                          _startup_timer;      ///< Timer to enable p2p consensus if fail to connect 
};
