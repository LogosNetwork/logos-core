#include <logos/consensus/network/peer_acceptor.hpp>

PeerAcceptor::PeerAcceptor(Service & service,
                           Log & log,
                           const Endpoint & local_endpoint,
                           PeerManager & manager)
    : _acceptor(service)
    , _service(service)
    , _log(log)
    , _local_endpoint(local_endpoint)
    , _manager(manager)
{}

void PeerAcceptor::Start(const std::set<Address> & server_endpoints)
{
    if(!server_endpoints.size())
    {
        return;
    }

    _server_endpoints = server_endpoints;

    _acceptor.open(_local_endpoint.protocol ());
    _acceptor.set_option(Acceptor::reuse_address (true));

    boost::system::error_code ec;
    _acceptor.bind(_local_endpoint, ec);

    if (ec)
    {
        BOOST_LOG (_log) << "PeerAcceptor - Error while binding for Consensus on "
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
       BOOST_LOG (_log) << "PeerAcceptor - Error while accepting peer connections: "
                        << ec.message();
       return;
    }

    BOOST_LOG (_log) << "PeerAcceptor - Connection accepted from "
                     << _accepted_endpoint;

    auto peer = _server_endpoints.find(_accepted_endpoint.address());

    if(peer == _server_endpoints.end())
    {
        BOOST_LOG (_log) << "PeerAcceptor - Unrecognized peer: "
                         << _accepted_endpoint.address();
    }
    else
    {
        _manager.OnConnectionAccepted(_accepted_endpoint, socket);
    }

    Accept();
}
