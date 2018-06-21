#include <rai/consensus/peer_acceptor.hpp>

PeerAcceptor::PeerAcceptor(boost::asio::io_service & service,
                           Log & log,
                           const Endpoint & local_endpoint,
                           PeerManager * manager)
    : acceptor_(service)
    , service_(service)
    , log_(log)
    , local_endpoint_(local_endpoint)
    , manager_(manager)
{}

void PeerAcceptor::Start(const std::set<Address> & server_endpoints)
{
    server_endpoints_ = server_endpoints;

    acceptor_.open(local_endpoint_.protocol ());
    acceptor_.set_option(Acceptor::reuse_address (true));

    boost::system::error_code ec;
    acceptor_.bind(local_endpoint_, ec);

    if (ec)
    {
        BOOST_LOG (log_) << "PeerAcceptor - Error while binding for Consensus on port " << local_endpoint_.port() << " :" << ec.message();

        throw std::runtime_error(ec.message ());
    }

    acceptor_.listen();
    Accept();
}

void PeerAcceptor::Accept()
{
    auto socket(std::make_shared<Socket> (service_));
    acceptor_.async_accept(*socket, accepted_endpoint_, [this, socket](boost::system::error_code const & ec) {
        OnAccept(ec, socket);
    });
}

void PeerAcceptor::OnAccept(boost::system::error_code const & ec, std::shared_ptr<Socket> socket)
{
    if (!ec)
    {
        BOOST_LOG (log_) << "PeerAcceptor - Connection accepted from " << accepted_endpoint_;

        auto peer = server_endpoints_.find(accepted_endpoint_.address());

        if(peer == server_endpoints_.end())
        {
            BOOST_LOG (log_) << "PeerAcceptor - Unrecognized peer: " << accepted_endpoint_.address();
        }
        else
        {
            manager_->OnConnectionAccepted(accepted_endpoint_, socket);
        }

        Accept();
    }
    else
    {
        BOOST_LOG (log_) << "PeerAcceptor - Error while accepting peer connections: " << ec.message();
    }
}
