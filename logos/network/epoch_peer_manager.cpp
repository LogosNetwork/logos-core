///
/// @file
/// This file contains definition of the EpochPeerManager class
/// which handles server connections from peers and binding of these connections
/// to the appropriate epoch during epoch transition
///
#include <logos/network/epoch_peer_manager.hpp>
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
    auto buf = std::make_shared<std::array<uint8_t, ConnectedClientIds::STREAM_SIZE>>();

    boost::asio::async_read(*socket,
                           boost::asio::buffer(buf->data(), ConnectedClientIds::STREAM_SIZE),
                           [this, endpoint, socket, buf](const ErrorCode &error, size_t size) {
        if (error)
        {
            LOG_ERROR(_log) << "EpochPeerManager::OnConnectionAccepted error: " << error.message();
            return;
        }
        bool stream_error = false;
        logos::bufferstream stream(buf->data(), ConnectedClientIds::STREAM_SIZE);
        auto ids = std::make_shared<ConnectedClientIds>(stream_error, stream);
        if (stream_error)
        {
            LOG_ERROR(_log) << "EpochPeerManager::OnConnectionAccepted deserialization error";
            return;
        }

        // check for bogus data
        if (ids->delegate_id > NUM_DELEGATES - 1
        || static_cast<int>(ids->connection) > 2
        || ids->epoch_number > ConsensusContainer::GetCurEpochNumber() + INVALID_EPOCH_GAP )
        {
            LOG_ERROR(_log) << "EpochPeerManager::OnConnectionAccepted - Likely received bogus data from unexpected connection.";
        }
        else
        {
            _peer_binder(Endpoint(boost::asio::ip::make_address_v4(ids->ip), endpoint.port()), socket, *ids);
        }
    });
}