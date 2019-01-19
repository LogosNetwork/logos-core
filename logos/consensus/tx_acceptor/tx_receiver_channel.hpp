// @file
// TxReceiverChannel class implements TxChannel to receive transactions from standalone
// TxAcceptor and forward them to ConsensusManager
//

#pragma once

#include <logos/consensus/tx_acceptor/tx_channel.hpp>
#include <logos/consensus/tx_acceptor/tx_message.hpp>
#include <logos/lib/log.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace logos { class node_config; class alarm; }

class TxReceiverChannel : public TxChannel
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

    logos::process_return OnSendRequest(std::shared_ptr<StateBlock> block, bool should_buffer = false)
    {
        return _receiver.OnSendRequest(block, should_buffer);
    }
    void Connect();
    void ReConnect();
    void OnConnect(const Error &ec);
    void AsyncReadHeader();
    void AsyncReadMessage(const TxMessage &header);
    template<typename F>
    void AsyncRead(size_t size, F&& f);
    template<typename F>
    void ProcessCallback(size_t size, F&& f);

    static constexpr std::chrono::seconds CONNECT_RETRY_DELAY{5};     ///< Reconnect delay in seconds.
    static constexpr size_t BUFFER_CAPACITY = 1024000;
    using Buffer  = std::array<uint8_t, BUFFER_CAPACITY>;

    Service &               _service;
    Endpoint                _endpoint;
    std::shared_ptr<Socket> _socket;
    logos::alarm &          _alarm;
    TxChannel &             _receiver;
    Log                     _log;
    Buffer                  _buffer;
    uint32_t                _buffer_size;

};
