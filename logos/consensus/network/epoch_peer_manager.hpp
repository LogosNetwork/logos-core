///
/// @file
/// This file contains declaration of the EpochPeerManager class
/// which handles server connections from peers and binding of these connections
/// to the appropriate epoch during epoch transition
///
#pragma once

#include <logos/consensus/consensus_manager_config.hpp>
#include <logos/consensus/network/peer_acceptor.hpp>
#include <logos/consensus/network/peer_manager.hpp>

struct KeyAdvertisement;

class PeerAcceptorStarter
{
public:
    using Address     = boost::asio::ip::address;
    ~PeerAcceptorStarter() = default;
    virtual void Start() = 0;
};

/// Handles call back on peer's accepted socket connection
/// The callback reads peer's public key/epoch/delegate id and
/// calls respective epoch manager to bind the channel to the
/// backup delegate
class EpochPeerManager : public PeerManager,
                         public PeerAcceptorStarter
{
    using Service     = boost::asio::io_service;
    using Endpoint    = boost::asio::ip::tcp::endpoint;
    using Socket      = boost::asio::ip::tcp::socket;
    using Config      = ConsensusManagerConfig;
    using PeerBinder  = std::function<void(const Endpoint, std::shared_ptr<Socket>, ConnectedClientIds)>;
    using ErrorCode   = boost::system::error_code;

public:
    /// Class constructor
    /// @param service boost asio service reference
    /// @param config node's configuration reference
    /// @param binder binds received peer connection to NetIOConsensus
    EpochPeerManager(Service&, const Config &, PeerBinder binder);
    ~EpochPeerManager() = default;

    /// Accepts connection from a peer
    /// @param endpoint remote peer's endpoint
    /// @param socket accepted socket
    void OnConnectionAccepted(const Endpoint endpoint, std::shared_ptr<Socket>) override;

    /// Get PeerAcceptor
    /// @param end_points peers endpoints
    void Start() override
    {
        _peer_acceptor.Start();
    }

private:
    PeerAcceptor    _peer_acceptor; ///< Accepts connections from peers
    Log             _log;           ///< Boost log
    PeerBinder      _peer_binder;   ///< Peer binding to NetIOConsensus
};
