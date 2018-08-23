//===-- logos/consensus/consensus_netio.hpp - ConsensusNetIOManager class declaration -------*- C++ -*-===//
//
// Open source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the ConsensusNetIOManager classes, which handles
/// network connections between the delegates
///
//===----------------------------------------------------------------------===//
#pragma once

#include <logos/consensus/consensus_netio.hpp>
#include <logos/consensus/peer_manager.hpp>
#include <logos/consensus/peer_acceptor.hpp>
#include <logos/consensus/consensus_manager_config.hpp>
#include <logos/consensus/delegate_key_store.hpp>

class IConsensusManager;

//! ConsensusNetIOManagare manages connections to peers
/*!
  Creates ConsensusNetIO instances either as the client to connect to remote peers
  or as accepted connection
*/
class ConsensusNetIOManager { //: public PeerManager {
    //! Aliases
    using Service   = boost::asio::io_service;
    using Endpoint  = boost::asio::ip::udp::endpoint;
    using Socket    = boost::asio::ip::udp::socket;
    using Log       = boost::log::sources::logger_mt;
    using Config    = ConsensusManagerConfig;
    using Address   = boost::asio::ip::address;
    using Delegates = std::vector<Config::Delegate>;
    using ConsensusManagers = std::map<ConsensusType, IConsensusManager&>;
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
        ConsensusManagers consensus_managers,
        Service & service, 
        logos::alarm & alarm, 
        const Config & config,
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
    //void OnConnectionAccepted(const Endpoint& endpoint, std::shared_ptr<Socket>) override;

    //! Bind connected net connections to ConsensusConnection
    /*!
      Calls registered consensus managers to bind netio channel to consensus connection
      \param netio channel
      \param delegate_id connected delegate id
    */
    void BindIOChannel(std::shared_ptr<ConsensusNetIO> netio, uint8_t delegate_id);

private:

    Delegates                                       _delegates; //!< List of all delegates
    ConsensusManagers                               _consensus_managers; //!< Dictionary of registered consensus managers
    std::vector<std::shared_ptr<ConsensusNetIO>>    _connections; //!< NetIO connections
    Log                                             _log; //!< boost asio log
    logos::alarm &                                  _alarm; //!< alarm
    //PeerAcceptor                                    _peer_acceptor; //!< PeerAcceptor instance
    DelegateKeyStore &                              _key_store; //!< Delegates public key store
    MessageValidator &                              _validator; //!< Validator/Signer of consensus messages
    std::recursive_mutex                            _connection_mutex; //!< NetIO connections access mutex
    std::recursive_mutex                            _bind_mutex; //!< NetIO consensus connections mutes
    uint8_t                                         _delegate_id; //!< The delegate id
};
