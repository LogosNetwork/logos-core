///
/// @file
/// This file contains definition of the EpochPeerManager class
/// which handles server connections from peers and binding of these connections
/// to the appropriate epoch during epoch transition
///
#include <logos/consensus/network/epoch_peer_manager.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/messages/util.hpp>

EpochPeerManager::EpochPeerManager(Service& service,
                                   const Config &config,
                                   PeerBinder binder)
    : _peer_acceptor(service, _log,
                     Endpoint(boost::asio::ip::make_address_v4(config.local_address),
                              config.peer_port), *this)
    , _peer_binder(binder)
{}

void
EpochPeerManager::OnConnectionAccepted(const EpochPeerManager::Endpoint &endpoint,
                                       std::shared_ptr<EpochPeerManager::Socket> socket)
{
    auto pub = std::make_shared<KeyAdvertisement>();

    boost::asio::async_read(*socket,
                           boost::asio::buffer(pub.get(),sizeof(KeyAdvertisement)),
                           [this, &endpoint, socket, pub](const ErrorCode &error, size_t size) {
        if (error)
        {
            BOOST_LOG(_log) << "EpochPeerManager::OnConnectionAccepted error: " << error.message();
            return;
        }

        if (pub->type != MessageType::Key_Advert || pub->consensus_type != ConsensusType::Any)
        {
            BOOST_LOG(_log) << "EpochPeerManager::OnConnectionAccepted invalid message "
                            << MessageToName(pub->type) << " " << ConsensusToName(pub->consensus_type);
            return;
        }

        _peer_binder(endpoint, socket, pub);
    });
}
