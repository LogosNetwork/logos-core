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

namespace logos { class node_config; }
struct StateBlock;

/// Json request container
struct json_request
{
    using Socket        = boost::asio::ip::tcp::socket;
    using Request       = boost::beast::http::request<boost::beast::http::string_body>;
    using Response      = boost::beast::http::response<boost::beast::http::string_body>;
    using Buffer        = boost::beast::flat_buffer;
    json_request(std::shared_ptr<Socket> s) : socket(s) {}
    std::shared_ptr<Socket>     socket; /// accepted socket
    Buffer                      buffer; /// buffer to receive json request
    Request                     request;/// request object
    Response                    res;    /// response object
};

class TxAcceptor;

/// Extends PeerManager with json/binary context to execute the right
/// type of callback when connection is accepted
class TxPeerManager : public PeerManager
{
    using Service       = boost::asio::io_service;
    using Socket        = boost::asio::ip::tcp::socket;
    using Reader        = void (TxAcceptor::*)(std::shared_ptr<Socket>);
    using Endpoint      = boost::asio::ip::tcp::endpoint;
public:
    /// Class constructor
    /// @param service boost asio service reference
    /// @param ip to accept client connection
    /// @param port to accept client connection
    /// @param tx_acceptor reference for reader callback
    /// @param reader reader callback for json/binary request
    TxPeerManager(Service & service, const std::string & ip, const uint16_t port,
                  TxAcceptor & tx_acceptor, Reader reader);
    /// Class distructor
    ~TxPeerManager() = default;

    /// Accepted connection callback
    /// @param endpoint of accepted connection
    /// @param socket  of accepted connection
    void OnConnectionAccepted(const Endpoint endpoint, std::shared_ptr<Socket> socket) override;
private:
    Service &       _service;       /// boost asio service reference
    Endpoint        _endpoint;      /// local endpoint
    PeerAcceptor    _peer_acceptor; /// acceptor's instance
    Reader          _reader;        /// json/binary member function pointer
    TxAcceptor &    _tx_acceptor;   /// tx acceptor reference to call reader with
    Log             _log;           /// boost asio log
};

/// TxAcceptor accepts client connection, reads json/binary transaction
/// validates transaction, and forwards it to TxChannel. Standalone TxAcceptor writes
/// transasction to the delegate. Delegate TxAcceptor passes transaction to ConsensusContainer (TxChannel).
class TxAcceptor
{
    using Service       = boost::asio::io_service;
    using Socket        = boost::asio::ip::tcp::socket;
    using Ptree         = boost::property_tree::ptree;
    using Error         = boost::system::error_code;
public:
    /// Delegate class constructor
    /// @param service boost asio service reference
    /// @param acceptor_channel is ConsensusContainer in this case
    /// @param config of the node
    TxAcceptor(Service & service, std::shared_ptr<TxChannel> acceptor_channel, logos::node_config & config);
    /// Standalone class constructor
    /// @param service boost asio service reference
    /// @param config of the node
    TxAcceptor(Service & service, logos::node_config & config);
    /// Class distructor
    ~TxAcceptor() = default;
private:
    /// Read json request
    /// @param socket of the connected client
    void AsyncReadJson(std::shared_ptr<Socket> socket);
    /// Read binary request
    /// @param socket of the connected client
    void AsyncReadBin(std::shared_ptr<Socket> socket);
    /// Respond to client with json message
    /// @param jrequest json request container
    /// @param tree json represented as ptree
    void RespondJson(std::shared_ptr<json_request> jrequest, const Ptree & tree);
    /// Respond to client with json message
    /// @param key of the response
    /// @param value of the response
    void RespondJson(std::shared_ptr<json_request> jrequest, const std::string & key, const std::string & value);
    /// Deserialize string to state block
    /// @param block_text serialized block
    /// @return StateBlock structure
    std::shared_ptr<StateBlock> ToStateBlock(const std::string &&block_text);
    /// Validate state block
    /// @param block state block
    /// @return result of the validation, 'progress' is success
    logos::process_result Validate(const std::shared_ptr<StateBlock> & block);

    Service &                       _service;           /// boost asio service reference
    TxPeerManager                   _json_peer;         /// json request connection acceptor
    TxPeerManager                   _bin_peer;          /// binary request connection acceptor
    TxAcceptorConfig                _config;            /// tx acceptor configuration
    std::shared_ptr<TxChannel>      _acceptor_channel;  /// transaction forwarding channel
    Log                             _log;               /// boost log
};
