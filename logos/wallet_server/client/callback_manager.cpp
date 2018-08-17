#include <logos/wallet_server/client/callback_manager.hpp>

void CallbackManager::OnCallbackDone(const Handle handle)
{
    std::lock_guard<std::mutex> lock(_mutex);

    _handlers.erase(handle);
}
