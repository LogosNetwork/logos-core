// @ file
// This file declares TxReceiver which receives transaction from TxAcceptors when TxAcceptors
// are configured as standalone.
//

#include <logos/tx_acceptor/tx_receiver_channel.hpp>
#include <logos/tx_acceptor/tx_receiver.hpp>
#include <logos/node/node.hpp>

TxReceiver::TxReceiver(Service &service,
                       logos::alarm &alarm,
                       std::shared_ptr<TxChannel> receiver,
                       logos::node_config &config)
    : _service(service)
    , _alarm(alarm)
    , _receiver(receiver)
    , _config(config.tx_acceptor_config)
{
    for (auto acceptor : _config.tx_acceptors)
    {
        _channels.push_back(std::make_shared<TxReceiverChannel>(service,
                                                                _alarm,
                                                                acceptor.ip,
                                                                acceptor.port,
                                                                *_receiver));
        LOG_INFO(_log) << "TxReceiver::TxReceiver created TxReceiverChannel "
                       << " ip " << acceptor.ip
                       << " port " << acceptor.port;
    }
}