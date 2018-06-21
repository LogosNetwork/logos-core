#pragma once

#include <rai/consensus/peer_manager.hpp>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/asio.hpp>

#include <memory>
#include <set>


class PeerAcceptor
{
    using Socket   = boost::asio::ip::tcp::socket;
    using Acceptor = boost::asio::ip::tcp::acceptor;
	using Endpoint = boost::asio::ip::tcp::endpoint;
	using Address  = boost::asio::ip::address;
    using Log      = boost::log::sources::logger_mt;

public:

	PeerAcceptor(boost::asio::io_service & service,
	             Log & log,
				 const Endpoint & local_endpoint,
				 PeerManager * manager);

	void Start(const std::set<Address> & server_endpoints);

	void Accept();

	void OnAccept(boost::system::error_code const & ec, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

private:

	std::set<Address>         server_endpoints_;
	Acceptor                  acceptor_;
	boost::asio::io_service & service_;
	Log &                     log_;
	Endpoint                  local_endpoint_;
	Endpoint                  accepted_endpoint_;
	PeerManager *             manager_;
};


