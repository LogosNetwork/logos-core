#pragma once

#include <rai/consensus/peer_manager.hpp>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/asio.hpp>

#include <memory>
#include <set>

class PeerAcceptor
{
    using Service  = boost::asio::io_service;
    using Socket   = boost::asio::ip::tcp::socket;
    using Acceptor = boost::asio::ip::tcp::acceptor;
	using Endpoint = boost::asio::ip::tcp::endpoint;
	using Address  = boost::asio::ip::address;
    using Log      = boost::log::sources::logger_mt;

public:

	PeerAcceptor(Service & service,
	             Log & log,
				 const Endpoint & local_endpoint,
				 PeerManager * manager);

	void Start(const std::set<Address> & server_endpoints);

	void Accept();

	void OnAccept(boost::system::error_code const & ec, std::shared_ptr<Socket> socket);

private:

	std::set<Address> _server_endpoints;
	Acceptor          _acceptor;
	Service &         _service;
	Log &             _log;
	Endpoint          _local_endpoint;
	Endpoint          _accepted_endpoint;
	PeerManager *     _manager;
};

