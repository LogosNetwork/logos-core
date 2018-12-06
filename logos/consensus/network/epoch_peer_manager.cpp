///
/// @file
/// This file contains definition of the EpochPeerManager class
/// which handles server connections from peers and binding of these connections
/// to the appropriate epoch during epoch transition
///
#include <logos/consensus/network/epoch_peer_manager.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/messages/util.hpp>
#include <logos/consensus/consensus_container.hpp>

EpochPeerManager::EpochPeerManager(Service& service,
                                   const Config &config,
                                   PeerBinder binder)
    : _peer_acceptor(service,
                     Endpoint(boost::asio::ip::make_address_v4(config.local_address),
                              config.peer_port), *this)
    , _peer_binder(binder)
{}

void
EpochPeerManager::OnConnectionAccepted(const EpochPeerManager::Endpoint endpoint,
                                       std::shared_ptr<EpochPeerManager::Socket> socket)
{
    auto ids = std::make_shared<ConnectedClientIds>();

    boost::asio::async_read(*socket,
                           boost::asio::buffer(ids.get(), sizeof(ConnectedClientIds)),
                           [this, endpoint, socket, ids](const ErrorCode &error, size_t size) {
        if (error)
        {
            LOG_ERROR(_log) << "EpochPeerManager::OnConnectionAccepted error: " << error.message();
            return;
        }
        // check for bogus data
        if (ids->delegate_id > NUM_DELEGATES - 1
        || static_cast<int>(ids->connection) > 2
        || ids->epoch_number > ConsensusContainer::GetCurEpochNumber() + 10 )
        {
            LOG_ERROR(_log) << "EpochPeerManager::OnConnectionAccepted - Likely received bogus data from unexpected connection.";
        }
        else
        {
            _peer_binder(endpoint, socket, *ids);
        }
    });
}
