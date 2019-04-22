// @file
// This file declares TxAcceptorChannel which provides communication between standalone TxAcceptor
// and Delegate

#pragma once

#include <logos/tx_acceptor/tx_acceptor_config.hpp>
#include <logos/tx_acceptor/tx_channel.hpp>
#include <logos/network/peer_acceptor.hpp>
#include <logos/network/peer_manager.hpp>
#include <logos/network/net_io_send.hpp>
#include <logos/lib/log.hpp>

#include <boost/asio/deadline_timer.hpp>

namespace logos { class node_config; }

/// Implements forwarding to the Delegate
class TxAcceptorChannel : public TxChannel,
                          public PeerManager,
                          public NetIOSend,
                          public Self<TxAcceptorChannel>
{
    using Service       = boost::asio::io_service;
    using Socket        = boost::asio::ip::tcp::socket;
    using Endpoint      = boost::asio::ip::tcp::endpoint;
    using Error         = boost::system::error_code;
    using Timer         = boost::asio::deadline_timer;
    using Seconds       = boost::posix_time::seconds;

public:
    /// Class constructor
    /// @param service boost asio service reference [in]
    /// @param config tx acceptor configuration [in]
    TxAcceptorChannel(Service & service, const TxAcceptorConfig &config);
    /// Class distruction
    ~TxAcceptorChannel() = default;

protected:

    /// Handle write error
    /// @param error on write
    void OnError(const Error &error) override;

private:
    static const Seconds TIMEOUT;
    static const uint16_t INACTIVITY; // milliseconds (40 seconds)

    /// Accepted connection callback
    /// @param endpoint of accepted connection [in]
    /// @param socket  of accepted connection [in]
    void OnConnectionAccepted(const Endpoint endpoint, shared_ptr<Socket>) override;
    /// Validate connected delegate
    /// @param socket of accepted [in]
    void Validate(std::shared_ptr<Socket> socket);
    /// Schedule timer
    /// @param timeout value [in]
    void ScheduleTimer(const Seconds & timeout);
    /// Handle heartbeat timeout
    /// @param error timer's error [in]
    void OnTimeout(const Error &error);
    /// Forward transaction to the delegate
    /// @param block transaction [in]
    /// @param should_buffer used in benchmarking [in]
    /// @return result of the operation
    logos::process_return OnDelegateMessage(std::shared_ptr<DM> block, bool should_buffer) override;
    /// Forwards transactions  for batch block consensus.
    /// Submits transactions to consensus logic.
    ///     @param blocks of transactions [in]
    ///     @return process_return result of the operation, if there is an error then returns a single
    ///             result code of the error
    Responses OnSendRequest(std::vector<std::shared_ptr<DM>> &) override;

    Service &               _service;               /// boost asio service reference
    Endpoint                _endpoint;              /// local endpoint
    PeerAcceptor            _delegate;              /// delegate's acceptor
    Log                     _log;                   /// boost log
    uint64_t                _last_sent;             /// last sent message's time stamp
    Timer                   _inactivity_timer;      /// inactivity timer
    std::mutex              _mutex;                 /// timer's mutex
    TxAcceptorConfig        _config;                /// tx acceptor configuration
    bls::PublicKey          _bls_pub;               /// delegate's bls public key
};
