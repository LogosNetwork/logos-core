// @file
// This file contains declaration of TxAcceptor which receives transactions from
// a client and forwards them to Delegate. TxAcceptor mitigates risk of DDoS attack.
// A delegate can have multiple TxAcceptors.
//

#pragma once

#include <logos/consensus/tx_acceptor/tx_acceptor_config.hpp>
#include <logos/consensus/tx_acceptor/tx_channel.hpp>
#include <logos/consensus/network/peer_acceptor.hpp>
#include <logos/consensus/network/peer_manager.hpp>
#include <logos/lib/log.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast.hpp>
//#include <boost/asio/read.hpp>

namespace logos { class node_config; }
struct StateBlock;

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

class TxAcceptor;

class TxPeerManager : public PeerManager
{
    using Service       = boost::asio::io_service;
    using Socket        = boost::asio::ip::tcp::socket;
    using Reader        = void (TxAcceptor::*)(std::shared_ptr<Socket>);
    using Endpoint      = boost::asio::ip::tcp::endpoint;
public:
    TxPeerManager(Service & service, const std::string & ip, const uint16_t port,
                  TxAcceptor & tx_acceptor, Reader reader);
    ~TxPeerManager() = default;

    void OnConnectionAccepted(const Endpoint endpoint, std::shared_ptr<Socket>) override;
private:
    Service &       _service;
    Endpoint        _endpoint;
    PeerAcceptor    _peer_acceptor;
    Reader          _reader;
    TxAcceptor &    _tx_acceptor;
    Log             _log;
};

class TxAcceptor
{
    using Service       = boost::asio::io_service;
    using Socket        = boost::asio::ip::tcp::socket;
    using Ptree         = boost::property_tree::ptree;
    using Error         = boost::system::error_code;
public:
    /// Class constructor
    /// @param service boost asio service reference
    TxAcceptor(Service & service, std::shared_ptr<TxChannel> acceptor_channel, logos::node_config & config);
    TxAcceptor(Service & service, logos::node_config & config);
    /// Class destructor
    ~TxAcceptor() = default;
private:
    void AsyncReadJson(std::shared_ptr<Socket> socket);
    void AsyncReadBin(std::shared_ptr<Socket> socket);
    void CommonInit(logos::node_config &config);
    void OnRead();
    void RespondJson(std::shared_ptr<json_request> jrequest, const Ptree & tree);
    void RespondJson(std::shared_ptr<json_request> jrequest, std::string key, std::string value);
    std::shared_ptr<StateBlock> ToStateBlock(const std::string &&block_text);

    Service &                       _service;
    TxPeerManager                   _json_peer;
    TxPeerManager                   _bin_peer;
    TxAcceptorConfig                _config;
    std::shared_ptr<TxChannel>      _acceptor_channel = nullptr;
    Log                             _log;
};
