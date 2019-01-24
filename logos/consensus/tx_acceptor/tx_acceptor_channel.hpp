// @file
// This file declares TxAcceptorChannel which provides communication between standalone TxAcceptor
// and Delegate

#pragma once

#include <logos/consensus/tx_acceptor/tx_channel.hpp>
#include <logos/consensus/network/peer_acceptor.hpp>
#include <logos/consensus/network/peer_manager.hpp>
#include <logos/consensus/network/net_io_send.hpp>
#include <logos/lib/log.hpp>

namespace logos { class node_config; }

/// Implements forwarding to the Delegate
class TxAcceptorChannel : public TxChannel,
                          public PeerManager,
                          public NetIOSend
{
    using Service       = boost::asio::io_service;
    using Socket        = boost::asio::ip::tcp::socket;
    using Endpoint      = boost::asio::ip::tcp::endpoint;
    using Error         = boost::system::error_code;

public:
    /// Class constructor
    /// @param service boost asio service reference [in]
    /// @param ip to accept delegate connection [in]
    /// @param port to accept delegate connection [in]
    TxAcceptorChannel(Service & service, const std::string & ip, const uint16_t port);
    /// Class distruction
    ~TxAcceptorChannel() = default;

protected:

    /// Handle write error
    /// @param error on write
    void OnError(const Error &error) override;

private:
    /// Accepted connection callback
    /// @param endpoint of accepted connection [in]
    /// @param socket  of accepted connection [in]
    void OnConnectionAccepted(const Endpoint endpoint, shared_ptr<Socket>) override;
    /// Validate connected delegate
    /// @param endpoint of accepted delegate [in]
    /// @param socket of accepted [in]
    /// @return true if validated
    bool Validate(const Endpoint endpoint, std::shared_ptr<Socket> socket)
    {
        return true; // TODO
    }
    /// Forward transaction to the delegate
    /// @param block transaction [in]
    /// @param should_buffer used in benchmarking [in] TODO
    logos::process_return OnSendRequest(std::shared_ptr<Request> block, bool should_buffer);
    /// Forwards transactions  for batch block consensus.
    /// Submits transactions to consensus logic.
    ///     @param blocks of transactions [in]
    ///     @return process_return result of the operation
    Responses OnSendRequest(std::vector<std::shared_ptr<Request>> &) override;

    Service &               _service;               /// boost asio service reference
    Endpoint                _endpoint;              /// local endpoint
    PeerAcceptor            _delegate;              /// delegate's acceptor
    Log                     _log;                   /// boost log
};
