#pragma once

#include <logos/wallet_server/client/callback_manager.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>

class WalletServerClient : public CallbackManager
{

    using Service  = boost::asio::io_service;
    using Endpoint = boost::asio::ip::tcp::endpoint;
    using Handle   = wallet_server::client::callback::Handle;

public:

    WalletServerClient(const Endpoint & callback_endpoint,
                       Service & service);

    void OnBatchBlock(const ApprovedBSB & block);

private:

    Endpoint  _callback_endpoint;
    Service & _service;
    Handle    _next_handle = 0;
};
