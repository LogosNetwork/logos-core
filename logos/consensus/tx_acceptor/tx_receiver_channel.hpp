// @file
// TxReceiverChannel class implements TxChannel to receive transactions from standalone
// TxAcceptor and forward them to ConsensusManager
//

#pragma once

#include <logos/consensus/tx_acceptor/tx_message_header.hpp>
#include <logos/consensus/network/net_io_assembler.hpp>
#include <logos/consensus/tx_acceptor/tx_channel.hpp>
#include <logos/lib/log.hpp>

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace logos { class node_config; class alarm; }

class TxReceiverErrorHandler
{
public:
    TxReceiverErrorHandler() = default;
    virtual ~TxReceiverErrorHandler() = default;
    virtual void ReConnect(bool) = 0;
};

/// Implementes network assembler
class TxReceiverNetIOAssembler : public NetIOAssembler
{
    using Socket        = boost::asio::ip::tcp::socket;
    using Error         = boost::system::error_code;
public:
    TxReceiverNetIOAssembler (std::shared_ptr<Socket> socket, TxReceiverErrorHandler &handler)
        : NetIOAssembler(socket)
        , _error_handler(handler)
    {}
protected:
    /// Handle netio error
    /// param ec error code
    void OnError(const Error& ec) override;
private:
    TxReceiverErrorHandler & _error_handler;    /// reference to the error handler object
};

class TxReceiverChannel : public TxReceiverErrorHandler
{
    using Service       = boost::asio::io_service;
    using Socket        = boost::asio::ip::tcp::socket;
    using Endpoint      = boost::asio::ip::tcp::endpoint;
    using Error         = boost::system::error_code;
    using Timer         = boost::asio::deadline_timer;
    using Seconds       = boost::posix_time::seconds;

public:
    TxReceiverChannel(Service & service, logos::alarm & alarm,
                      const std::string & ip, const uint16_t port,
                      TxChannel & receiver);
    ~TxReceiverChannel() = default;

private:

    /// Connect to standalone TxAcceptor
    void Connect();
    /// Re-connect to standalone TxAcceptor
    /// @param cancel timer
    void ReConnect(bool cancel) override;
    /// On connect callback
    /// @param ec connection error
    void OnConnect(const Error &ec);
    /// Async read of the transaction's header
    void AsyncReadHeader();
    /// Async read of the message
    /// @param header of the message
    void AsyncReadMessage(const TxMessageHeader &header);
     /// Schedule timer
    /// @param timeout value [in]
    void ScheduleTimer(const Seconds & timeout);
    /// Handle heartbeat timeout
    /// @param error timer's error [in]
    void OnTimeout(const Error &error);

    static const std::chrono::seconds CONNECT_RETRY_DELAY;     ///< Reconnect delay in seconds.
    static const Seconds TIMEOUT; // 15 seconds
    static const uint16_t INACTIVITY; // milliseconds (60 seconds)

    Service &                   _service;           /// boost asio service reference
    Endpoint                    _endpoint;          /// local endpoint
    std::shared_ptr<Socket>     _socket;            /// connected socket
    logos::alarm &              _alarm;             /// logos's alarm reference
    TxChannel &                 _receiver;          /// channel to forward transactions
    Log                         _log;               /// boost log
    TxReceiverNetIOAssembler    _assembler;         /// assembles messages from TCP buffer
    uint64_t                    _last_received;     /// last received message timestamp
    Timer                       _inactivity_timer;  /// inactivity timer
    std::mutex                  _mutex;             /// timer's mutex
};
