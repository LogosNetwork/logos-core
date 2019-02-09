// @file
// This file contains declaration of TxAcceptor which receives transactions from
// a client and forwards them to Delegate. TxAcceptor mitigates risk of DDoS attack.
// A delegate can have multiple TxAcceptors.
//

#pragma once

#include <logos/tx_acceptor/tx_acceptor_channel.hpp>
#include <logos/tx_acceptor/tx_acceptor_config.hpp>
#include <logos/tx_acceptor/tx_channel.hpp>
#include <logos/network/peer_acceptor.hpp>
#include <logos/network/peer_manager.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/log.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast.hpp>

namespace logos { class node_config; }

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
    /// @param service boost asio service reference [in]
    /// @param ip to accept client connection [in]
    /// @param port to accept client connection [in]
    /// @param tx_acceptor reference for reader callback [in]
    /// @param reader reader callback for json/binary request [in]
    TxPeerManager(Service & service, const std::string & ip, const uint16_t port,
                  TxAcceptor & tx_acceptor, Reader reader);
    /// Class destructor
    ~TxPeerManager() = default;

    /// Accepted connection callback
    /// @param endpoint of accepted connection [in]
    /// @param socket  of accepted connection [in]
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
public:
    /// Checks if a client connection doesn't exceed max_connections
    /// @param socket accepted socket
    /// @returns true if the client's connection can be accpted
    bool CanAcceptClientConnection(std::shared_ptr<boost::asio::ip::tcp::socket> socket);

private:
    friend class TxAcceptorStandalone;
    friend class TxAcceptorDelegate;

    using Service       = boost::asio::io_service;
    using Socket        = boost::asio::ip::tcp::socket;
    using Ptree         = boost::property_tree::ptree;
    using Error         = boost::system::error_code;
    using Responses     = std::vector<std::pair<logos::process_result, BlockHash>>;
    using Request       = RequestMessage<ConsensusType::BatchStateBlock>;
    using Blocks        = std::vector<std::shared_ptr<Request>>;

    /// Delegate class constructor
    /// @param service boost asio service reference [in]
    /// @param acceptor_channel is ConsensusContainer in this case [in]
    /// @param config of the node [in]
    TxAcceptor(Service & service, std::shared_ptr<TxChannel> acceptor_channel, logos::node_config & config);
    /// Standalone class constructor
    /// @param service boost asio service reference [in]
    /// @param config of the node [in]
    TxAcceptor(Service & service, logos::node_config & config);
    /// Class destructor
    virtual ~TxAcceptor() = default;

    static constexpr uint32_t  MAX_REQUEST_SIZE = (sizeof(StateBlock) +
            sizeof(StateBlock::Transaction) * StateBlock::MAX_TRANSACTION) * 1500;
    static constexpr uint32_t BLOCK_SIZE_SIZE = sizeof(uint32_t);

    /// Read json request
    /// @param socket of the connected client [in]
    void AsyncReadJson(std::shared_ptr<Socket> socket);
    /// Read binary request
    /// @param socket of the connected client [in]
    void AsyncReadBin(std::shared_ptr<Socket> socket);
    /// Respond to client with json message
    /// @param jrequest json request container [in]
    /// @param tree json represented as ptree [in]
    void RespondJson(std::shared_ptr<json_request> jrequest, const Ptree & tree);
    /// Respond to client with json message
    /// @param key of the response [in]
    /// @param value of the response [in]
    void RespondJson(std::shared_ptr<json_request> jrequest, const std::string & key, const std::string & value);
    /// Respond to client with json message
    /// @param jrequest json request container [in]
    /// @param tree json represented as ptree [in]
    void RespondJson(std::shared_ptr<json_request> jrequest, const Responses &response);
    /// Respond to client with binary message
    /// @param socket to respond to [in]
    /// @param response to send [in]
    void RespondBin(std::shared_ptr<Socket> socket, const Responses &&response);
    /// Deserialize string to state block
    /// @param block_text serialized block [in]
    /// @return StateBlock structure
    std::shared_ptr<Request> ToRequest(const std::string &block_text);
    /// Validate state block
    /// @param block state block [in]
    /// @return result of the validation, 'progress' is success
    logos::process_result Validate(const std::shared_ptr<Request> & block);
    /// Validate/send received transaction for consensus protocol
    /// @param block received transaction [in]
    /// @param blocks to aggregate in delegate mode [in|out]
    /// @param response object [in|out]
    /// @param should_buffer benchmarking flag [in]
    void ProcessBlock(std::shared_ptr<Request> block, Blocks &blocks,
                      Responses &response, bool should_buffer = false);
    /// Run post processing once all blocks are processed individually
    /// @param blocks all valid blocks [in]
    /// @param response object [in|out]
    virtual void PostProcessBlocks(Blocks &blocks, Responses &response)
    {
        auto res = _acceptor_channel->OnSendRequest(blocks);
        if (res[0].first == logos::process_result::initializing)
        {
            response.clear();
            response.push_back({res[0].first, 0});
        }
    }
    /// Send or aggregate validated requests, default is to aggregate
    /// @param block received transaction [in]
    /// @param blocks to aggregate in delegate mode [in|out]
    /// @param response object [in|out]
    /// @param should_buffer benchmarking flag [in]
    virtual logos::process_result OnSendRequest(std::shared_ptr<Request> block, Blocks &blocks,
                                                Responses &response, bool should_buffer = false)
    {
        blocks.push_back(block);
        return logos::process_result::progress;
    }

    class ConnectionsManager {
    public:
        ConnectionsManager(std::atomic<uint32_t> &cur_connections)
            : _cur_connections(cur_connections)
        {}
        ~ConnectionsManager()
        {
            _cur_connections--;
        }

        std::atomic<uint32_t>&  _cur_connections;
    };

    Service &                       _service;           /// boost asio service reference
    TxPeerManager                   _json_peer;         /// json request connection acceptor
    TxPeerManager                   _bin_peer;          /// binary request connection acceptor
    TxAcceptorConfig                _config;            /// tx acceptor configuration
    std::shared_ptr<TxChannel>      _acceptor_channel;  /// transaction forwarding channel
    Log                             _log;               /// boost log
    std::atomic<uint32_t>           _cur_connections;   /// count of current connections
};

/// Standalone parses batch request and sends transactions one at a time.
/// The buffering is handled on the sending side by NetIOSend
/// and on receiving side by NetIOAssembler.
class TxAcceptorStandalone : public TxAcceptor
{
public:
    /// Standalone class constructor
    /// @param service boost asio service reference [in]
    /// @param config of the node [in]
    TxAcceptorStandalone(Service & service, logos::node_config & config)
        : TxAcceptor(service, config)
    {}
    ~TxAcceptorStandalone() = default;
};

/// Delegate parses batch request and sends transactions as vector.
class TxAcceptorDelegate : public TxAcceptor
{
public:
    /// Delegate class constructor
    /// @param service boost asio service reference [in]
    /// @param acceptor_channel is ConsensusContainer in this case [in]
    /// @param config of the node [in]
    TxAcceptorDelegate(Service & service, std::shared_ptr<TxChannel> acceptor_channel, logos::node_config & config)
        : TxAcceptor(service, acceptor_channel, config)
    {}
    ~TxAcceptorDelegate() = default;
};
