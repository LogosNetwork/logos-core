// @file
// This file contains implementation of TxAcceptor which receives transactions from
// a client and forwards them to Delegate. TxAcceptor mitigates risk of DDoS attack.
// A delegate can have multiple TxAcceptors.
//

#include <logos/consensus/tx_acceptor/tx_acceptor_config.hpp>
#include <logos/consensus/tx_acceptor/tx_acceptor.hpp>
#include <logos/node/node.hpp>

TxAcceptor::TxAcceptor(TxAcceptor::Service &service,
                       std::shared_ptr<TxChannel> acceptor_channel,
                       logos::node_config & config)
    : _service(service)
    , _acceptor(service)
    , _acceptor_channel(acceptor_channel)
    , _config(config.tx_acceptor_config)
{
    Start(_config.json_port, &TxAcceptor::AsyncReadJson);
    Start(_config.bin_port, &TxAcceptor::AsyncReadBin);
}

TxAcceptor::TxAcceptor(TxAcceptor::Service &service,
                       logos::node_config & config)
        : _service(service)
        , _acceptor(service)
        , _config(config.tx_acceptor_config)
{
    Start(0, &TxAcceptor::AsyncReadJson);
}

void
TxAcceptor::Start(uint16_t port, Reader r)
{
    auto endpoint = Endpoint(boost::asio::ip::make_address_v4(_ip), port);
    _acceptor.open(endpoint.protocol());
    _acceptor.set_option(Acceptor::reuse_address(true));
    boost::system::error_code ec;
    _acceptor.bind(endpoint, ec);

    if (ec)
    {
        LOG_ERROR (_log) << "TxAcceptor - Error while binding on "
                         << endpoint << " - "
                         << ec.message();

        throw std::runtime_error(ec.message());
    }

    _acceptor.listen();

    Accept(r);
}

void
TxAcceptor::Accept(Reader r)
{
    auto accepted_endpoint = std::make_shared<Endpoint>();
    auto socket(std::make_shared<Socket> (_service));
    _acceptor.async_accept(*socket, *accepted_endpoint,
                           [this, socket, accepted_endpoint, r](const Error & ec) {
                               OnAccept(ec, socket, accepted_endpoint, r);
                           });
}

void
TxAcceptor::OnAccept(const Error &ec,
                     std::shared_ptr<Socket> socket,
                     std::shared_ptr<Endpoint> accepted_endpoint,
                     Reader r)
{
    // TODO : have to validate connected delegate
    (this->*r)(socket);
}

void
TxAcceptor::AsyncReadJson(std::shared_ptr<Socket> socket)
{
    auto request = std::make_shared<json_request>(socket);
}

void
TxAcceptor::AsyncReadBin(std::shared_ptr<Socket> socket)
{
}

void
TxAcceptor::OnRead()
{}
