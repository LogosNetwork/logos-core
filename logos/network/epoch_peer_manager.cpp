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
                                   PeerBinder & binder)
    : _peer_acceptor(service,
                     Endpoint(boost::asio::ip::make_address_v4(config.local_address),
                              config.peer_port), *this)
    , _peer_binder(binder)
{}

void
EpochPeerManager::OnConnectionAccepted(const EpochPeerManager::Endpoint endpoint,
                                       std::shared_ptr<EpochPeerManager::Socket> socket)
{
    auto port = endpoint.port();
    _peer_binder.GetIdentityManager().ServerHandshake(socket, _peer_binder, [this, socket, port](std::shared_ptr<AddressAd> ad) {
        if (!ad)
        {
            LOG_DEBUG(_log) << "EpochPeerManager::OnConnectionAccepted, failed to read client's ad";
            socket->close();
        }
        else
        {
            if(ad->consensus_version != logos_version)
            {
                LOG_ERROR(_log) << "EpochPeerManager::OnConnectionAccepted, consensus version mismatch,"
                                << " peer version=" << (int)ad->consensus_version
                                << " my version=" << (int)logos_version;
                socket->close();
            } else {
                auto res = _peer_binder.Bind(socket,
                                             Endpoint(boost::asio::ip::make_address_v4(ad->GetIP().c_str()), port),
                                             ad->epoch_number,
                                             ad->delegate_id);
                if (!res)
                {
                    socket->close();
                }
                else
                {
                    _peer_binder.GetIdentityManager().UpdateAddressAd(*ad);
                }
            }
        }
    });
}
