#include <logos/wallet_server/client/wallet_server_client.hpp>

WalletServerClient::WalletServerClient(const Endpoint & callback_endpoint,
                                       Service & service)
    : _callback_endpoint(callback_endpoint)
    , _service(service)
{}

void WalletServerClient::OnBatchBlock(const ApprovedBSB & block)
{
    std::lock_guard<std::mutex> lock(_mutex);

    _handlers.emplace(std::piecewise_construct,
                      std::forward_as_tuple(_next_handle),
                      std::forward_as_tuple(block, _callback_endpoint, _service,
                                            this, _next_handle++));
}
