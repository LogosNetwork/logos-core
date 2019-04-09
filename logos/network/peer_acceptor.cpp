#include <logos/network/peer_acceptor.hpp>
#include <logos/identity_management/delegate_identity_manager.hpp>

PeerAcceptor::PeerAcceptor(Service & service,
                           const Endpoint & local_endpoint,
                           PeerManager & manager)
    : _acceptor(service)
    , _service(service)
    , _local_endpoint(local_endpoint)
    , _manager(manager)
{}

void PeerAcceptor::Start()
{
    if (_acceptor.is_open())
    {
        LOG_WARN(_log) << "PeerAcceptor::Start, acceptor is already active "
                        << (int)DelegateIdentityManager::GetGlobalDelegateIdx() << " "
                        << DelegateIdentityManager::GetDelegateIP();
        return;
    }

    _acceptor.open(_local_endpoint.protocol ());
    _acceptor.set_option(Acceptor::reuse_address (true));

    boost::system::error_code ec;
    _acceptor.bind(_local_endpoint, ec);

    if (ec)
    {
        LOG_ERROR (_log) << "PeerAcceptor - Error while binding for Consensus on "
                         << _local_endpoint << " - "
                         << ec.message();

        throw std::runtime_error(ec.message());
    }

    _acceptor.listen();
    Accept();
}

void PeerAcceptor::Accept()
{
    auto socket(std::make_shared<Socket> (_service));
    _acceptor.async_accept(*socket, _accepted_endpoint,
                           [this, socket](boost::system::error_code const & ec) {
                               OnAccept(ec, socket);
                           });
}

void PeerAcceptor::OnAccept(boost::system::error_code const & ec, std::shared_ptr<Socket> socket)
{
    if (ec)
    {
       LOG_ERROR (_log) << "PeerAcceptor - Error while accepting peer connections: "
                        << ec.message();
       return;
    }

    LOG_INFO (_log) << "PeerAcceptor - Connection accepted from "
                    << _accepted_endpoint;

    _manager.OnConnectionAccepted(_accepted_endpoint, socket);

    Accept();
}
