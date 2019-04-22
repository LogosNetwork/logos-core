// @ file
// This file declares TxReceiver which receives transaction from TxAcceptors when TxAcceptors
// are configured as standalone.
//
#pragma once

#include <logos/tx_acceptor/tx_acceptor_config.hpp>
#include <logos/lib/log.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast.hpp>

namespace logos { class node_config; class alarm; }
class TxReceiverChannel;
class TxChannel;

/// Receive transaction from TxAcceptor
class TxReceiver
{
    using Service       = boost::asio::io_service;
    using TxChannelPtr  = std::shared_ptr<TxReceiverChannel>;

public:
    /// Class constructor
    /// @param service boost asio service reference [in]
    /// @param alarm logos alarm reference [in]
    /// @param receiver channel to pass transaction for consensus protocol [in]
    /// @param config of the node [in]
    TxReceiver(Service & service, logos::alarm &alarm,
               std::shared_ptr<TxChannelExt> receiver, logos::node_config &config);
    /// Class destructor
    ~TxReceiver() = default;

private:
    Service &                      _service;   /// boost asio service reference
    logos::alarm &                 _alarm;     /// logos alarm reference
    TxAcceptorConfig               _config;    /// TxAcceptor configuration
    std::shared_ptr<TxChannelExt>  _receiver;  /// channel receiving TxAcceptor transactions
    std::vector<TxChannelPtr>      _channels;  /// channels to receive TxAcceptor transactions
    Log                            _log;       /// boost log
};
