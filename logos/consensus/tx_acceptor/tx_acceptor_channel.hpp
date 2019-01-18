// @file
// This file declares TxAcceptorChannel which provides communication between standalone TxAcceptor
// and Delegate

#pragma once

#include <logos/consensus/tx_acceptor/tx_channel.hpp>
#include <logos/consensus/network/peer_acceptor.hpp>
#include <logos/consensus/network/peer_manager.hpp>
#include <logos/lib/log.hpp>

namespace logos { class node_config; }

/// Implements forwarding to the Delegate
class TxAcceptorChannel : public TxChannel,
                          public PeerManager
{
    using Service       = boost::asio::io_service;
    using Socket        = boost::asio::ip::tcp::socket;
    using Endpoint      = boost::asio::ip::tcp::endpoint;
    using Error         = boost::system::error_code;
    using QueuedWrites  = std::list<std::shared_ptr<std::vector<uint8_t>>>;

public:
    /// Class constructor
    /// @param service boost asio service reference
    /// @param ip to accept delegate connection
    /// @param port to accept delegate connection
    TxAcceptorChannel(Service & service, const std::string & ip, const uint16_t port);
    /// Class distruction
    ~TxAcceptorChannel() = default;

private:
    /// Accepted connection callback
    /// @param endpoint of accepted connection
    /// @param socket  of accepted connection
    void OnConnectionAccepted(const Endpoint endpoint, shared_ptr<Socket>) override;
    /// Validate connected delegate
    /// @param endpoint of accepted delegate
    /// @param socket of accepted
    /// @return true if validated
    bool Validate(const Endpoint endpoint, std::shared_ptr<Socket> socket)
    {
        return true; // TODO
    }
    /// Forward transaction to the delegate
    /// @param block transaction
    /// @param should_buffer used in benchmarking TODO
    logos::process_return OnSendRequest(std::shared_ptr<StateBlock> block, bool should_buffer);
    /// Send everything on the queue
    void SendQueue();

    Service &               _service;               /// boost asio service reference
    Endpoint                _endpoint;              /// local endpoint
    PeerAcceptor            _delegate;              /// delegate's acceptor
    Log                     _log;                   /// boost log
    std::shared_ptr<Socket> _socket = nullptr;      /// accepted socket
    std::mutex              _send_mutex;            /// protect concurrent writes
    QueuedWrites            _queued_writes;         /// data waiting to get sent on the network
    uint32_t                _queue_reservation = 0; /// how many queued entries are being sent
    bool                    _sending = false;       /// is an async write in progress
};
