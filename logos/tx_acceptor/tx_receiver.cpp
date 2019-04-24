// @ file
// This file declares TxReceiver which receives transaction from TxAcceptors when TxAcceptors
// are configured as standalone.
//

#include <logos/tx_acceptor/tx_receiver_channel.hpp>
#include <logos/tx_acceptor/tx_receiver.hpp>
#include <logos/node/node.hpp>

TxReceiver::TxReceiver(Service &service,
                       logos::alarm &alarm,
                       std::shared_ptr<TxChannelExt> receiver,
                       logos::node_config &config)
    : _service(service)
    , _alarm(alarm)
    , _receiver(receiver)
    , _config(config)
{
}

void
TxReceiver::Start()
{
    for (auto acceptor : _config.tx_acceptor_config.tx_acceptors)
    {
        _channels.push_back(std::make_shared<TxReceiverChannel>(_service,
                                                                _alarm,
                                                                acceptor.ip,
                                                                acceptor.port,
                                                                _receiver,
                                                                _config));
        LOG_INFO(_log) << "TxReceiver::TxReceiver created TxReceiverChannel "
                       << " ip " << acceptor.ip
                       << " port " << acceptor.port;
    }
}

bool
TxReceiver::AddChannel(const std::string &ip, uint16_t port)
{
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = std::find_if(_channels.begin(),
                           _channels.end(),
                           [&ip,port](const auto &item){return item->Equal(ip,port);});
    if (it != _channels.end())
    {
        return false;
    }
    _channels.push_back(std::make_shared<TxReceiverChannel>(_service,
                                                            _alarm,
                                                            ip,
                                                            port,
                                                            _receiver,
                                                            _config));
    LOG_INFO(_log) << "TxReceiver::TxReceiver created TxReceiverChannel "
                   << " ip " << ip
                   << " port " << port;
    return true;
}

bool
TxReceiver::DeleteChannel(const std::string &ip, uint16_t port)
{
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = std::find_if(_channels.begin(),
                           _channels.end(),
                           [&ip,port](const auto &item){return item->Equal(ip,port);});
    if (it == _channels.end() || _channels.size() == 1)
    {
        return false;
    }

    _channels.erase(it);

    return true;
}
