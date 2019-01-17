// @file
// This file contains declaration of TxAcceptor which receives transactions from
// a client and forwards them to Delegate. TxAcceptor mitigates risk of DDoS attack.
// A delegate can have multiple TxAcceptors.
//

#pragma once

#include <logos/consensus/tx_acceptor/tx_acceptor_config.hpp>
#include <logos/consensus/tx_acceptor/tx_channel.hpp>
#include <logos/lib/log.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast.hpp>
//#include <boost/asio/read.hpp>

namespace logos { class node_config; }

struct json_request
{
    using Socket        = boost::asio::ip::tcp::socket;
    using Request       = boost::beast::http::request<boost::beast::http::string_body>;
    using Response      = boost::beast::http::response<boost::beast::http::string_body>;
    using Buffer        = boost::beast::flat_buffer;
    json_request(std::shared_ptr<Socket> s) : socket(s) {}
    std::shared_ptr<Socket>     socket;
    Buffer                      buffer;
    Request                     request;
    Response                    res;
};

class TxAcceptor
{
    using Service       = boost::asio::io_service;
    using Endpoint      = boost::asio::ip::tcp::endpoint;
    using Socket        = boost::asio::ip::tcp::socket;
    using Address       = boost::asio::ip::address;
    using Acceptor      = boost::asio::ip::tcp::acceptor;
    using Error         = boost::system::error_code;
    using Reader        = void (TxAcceptor::*)(std::shared_ptr<Socket>);

public:
    /// Class constructor
    /// @param service boost asio service reference
    TxAcceptor(Service & service, std::shared_ptr<TxChannel> acceptor_channel, logos::node_config & config);
    TxAcceptor(Service & service, logos::node_config & config);
    /// Class destructor
    ~TxAcceptor() = default;
private:
    void Start(uint16_t port, Reader r);
    void Accept(Reader r);
    void OnAccept(const Error &ec, std::shared_ptr<Socket> socket,
                  std::shared_ptr<Endpoint> accepted_endpoint, Reader r);
    void AsyncReadJson(std::shared_ptr<Socket> socket);
    void AsyncReadBin(std::shared_ptr<Socket> socket);
    void OnRead();

    Service &                   _service;
    Acceptor                    _acceptor;
    TxAcceptorConfig            _config;
    std::string                 _ip;
    uint16_t                    _bin_port = 0;
    uint16_t                    _json_port = 0;
    std::shared_ptr<Socket>     _socket;
    std::shared_ptr<TxChannel>  _acceptor_channel;
    Log                         _log;
};
