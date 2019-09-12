#pragma once

#include <logos/network/peer_manager.hpp>
#include <logos/lib/log.hpp>

#include <boost/asio.hpp>

#include <memory>
#include <set>

class PeerAcceptor : public std::enable_shared_from_this<PeerAcceptor>
{
    using Service  = boost::asio::io_service;
    using Socket   = boost::asio::ip::tcp::socket;
    using Acceptor = boost::asio::ip::tcp::acceptor;
    using Endpoint = boost::asio::ip::tcp::endpoint;
    using Address  = boost::asio::ip::address;

public:

    PeerAcceptor(Service & service,
                 const Endpoint & local_endpoint,
                 PeerManager & manager);
    ~PeerAcceptor() = default;

    void Start();

    void Accept();

    void OnAccept(boost::system::error_code const & ec, std::shared_ptr<Socket> socket);

private:

    Acceptor        _acceptor;
    Log             _log;
    Service &       _service;
    Endpoint        _local_endpoint;
    Endpoint        _accepted_endpoint;
    PeerManager &   _manager;
};

