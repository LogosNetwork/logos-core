/// @file
/// This file contains the declaration of the ConsensusNetIOManager classes, which handles
/// network connections between the delegates
#pragma once

#include <logos/consensus/consensus_manager_config.hpp>
#include <logos/consensus/network/consensus_netio.hpp>
#include <logos/consensus/network/peer_acceptor.hpp>
#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/peer_manager.hpp>

class ChannelBinder;

/// ConsensusNetIOManager manages connections to peers.
///
/// Creates ConsensusNetIO instances either as the client to connect
/// to remote peers or as an accepted connection.
class ConsensusNetIOManager : public PeerManager
{

    using Service   = boost::asio::io_service;
    using Endpoint  = boost::asio::ip::tcp::endpoint;
    using Socket    = boost::asio::ip::tcp::socket;
    using Log       = boost::log::sources::logger_mt;
    using Config    = ConsensusManagerConfig;
    using Address   = boost::asio::ip::address;
    using Delegates = std::vector<Config::Delegate>;

    //TODO: Should be unordered_map
    using Managers    = std::map<ConsensusType, ChannelBinder&>;
    using Connections = std::vector<std::shared_ptr<ConsensusNetIO>>;

public:

    /// Class constructor.
    ///
    /// The constructor is called by node.
    ///     @param consensus_managers maps ConsensusType to ConsensusManager
    ///     @param service reference to boost asio service
    ///     @param reference to alarm
    ///     @param config reference to consensus manager configuration
    ///     @param key_store delegates' public key store
    ///     @param validator validator/signer of consensus messages
    ConsensusNetIOManager(
        Managers consensus_managers,
        Service & service, 
        logos::alarm & alarm, 
        const Config & config,
        DelegateKeyStore & key_store,
        MessageValidator & validator); 

    ~ConsensusNetIOManager() = default;

    /// Server connection accepted call back.
    ///
    /// Called by PeerAcceptor.
    ///     @param endpoint connected peer endpoint
    ///     @param socket connected peer socket
    void OnConnectionAccepted(const Endpoint& endpoint,
                              std::shared_ptr<Socket>) override;

    /// Bind connected IO Channel to ConsensusConnection.
    ///
    /// Calls registered consensus managers to bind netio
    /// channel to consensus connection.
    ///     @param netio channel
    ///     @param delegate_id connected delegate id
    void BindIOChannel(std::shared_ptr<ConsensusNetIO> netio,
                       uint8_t delegate_id);

private:

    Delegates            _delegates;             ///< List of all delegates
    Managers             _consensus_managers;    ///< Dictionary of registered consensus managers
    Connections          _connections;           ///< NetIO connections
    Log                  _log;                   ///< boost asio log
    logos::alarm &       _alarm;                 ///< alarm
    PeerAcceptor         _peer_acceptor;         ///< PeerAcceptor instance
    DelegateKeyStore &   _key_store;             ///< Delegates' public key store
    MessageValidator &   _validator;             ///< Validator/Signer of consensus messages
    std::recursive_mutex _connection_mutex;      ///< NetIO connections access mutex
    std::recursive_mutex _bind_mutex;            ///< NetIO consensus connections mutex
    uint8_t              _delegate_id;           ///< The local delegate id
};
