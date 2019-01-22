// @file
// TxReceiverChannel class implements TxChannel to receive transactions from standalone
// TxAcceptor and forward them to ConsensusManager
//

#pragma once

#include <logos/consensus/tx_acceptor/tx_message_header.hpp>
#include <logos/consensus/network/net_io_assembler.hpp>
#include <logos/consensus/tx_acceptor/tx_channel.hpp>
#include <logos/lib/log.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace logos { class node_config; class alarm; }

class TxReceiverErrorHandler
{
public:
    TxReceiverErrorHandler() = default;
    virtual ~TxReceiverErrorHandler() = default;
    virtual void ReConnect() = 0;
};

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
    void OnError(const Error&) override;
private:
    TxReceiverErrorHandler & _error_handler;
};

class TxReceiverChannel : public TxReceiverErrorHandler
{
    using Service       = boost::asio::io_service;
    using Socket        = boost::asio::ip::tcp::socket;
    using Endpoint      = boost::asio::ip::tcp::endpoint;
    using Error         = boost::system::error_code;

public:
    TxReceiverChannel(Service & service, logos::alarm & alarm,
                      const std::string & ip, const uint16_t port,
                      TxChannel & receiver);
    ~TxReceiverChannel() = default;

private:

    void Connect();
    void ReConnect() override;
    void OnConnect(const Error &ec);
    void AsyncReadHeader();
    void AsyncReadMessage(const TxMessageHeader &header);

    static constexpr std::chrono::seconds CONNECT_RETRY_DELAY{5};     ///< Reconnect delay in seconds.

    Service &                   _service;
    Endpoint                    _endpoint;
    std::shared_ptr<Socket>     _socket;
    logos::alarm &              _alarm;
    TxChannel &                 _receiver;
    Log                         _log;
    TxReceiverNetIOAssembler    _assembler;
};
