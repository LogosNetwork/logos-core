// @ file
// This file declares TxReceiver which receives transaction from TxAcceptors when TxAcceptors
// are configured as standalone.
//

#include <logos/consensus/tx_acceptor/tx_receiver.hpp>
#include <logos/node/node.hpp>

TxReceiver::TxReceiver(Service &service,
                       std::shared_ptr<TxChannel> receiver,
                       logos::node_config &config)
    : _service(service)
    , _receiver(receiver)
    , _config(config.tx_acceptor_config)
{}