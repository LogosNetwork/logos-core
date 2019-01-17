// @ file
// This file declares TxReceiver which receives transaction from TxAcceptors when TxAcceptors
// are configured as standalone.
//
#pragma once

#include <logos/consensus/tx_acceptor/tx_acceptor_config.hpp>
#include <logos/consensus/tx_acceptor/tx_channel.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast.hpp>

namespace logos { class node_config; }

class TxReceiver
{
    using Service       = boost::asio::io_service;
    using Endpoint      = boost::asio::ip::tcp::endpoint;
    using Socket        = boost::asio::ip::tcp::socket;
    using Address       = boost::asio::ip::address;
    using Acceptor      = boost::asio::ip::tcp::acceptor;
    using Error         = boost::system::error_code;

public:
    TxReceiver(Service & service, std::shared_ptr<TxChannel> receiver, logos::node_config &config);
    ~TxReceiver() = default;

private:
    Service &                   _service;
    TxAcceptorConfig            _config;
    std::shared_ptr<TxChannel>  _receiver;
};
