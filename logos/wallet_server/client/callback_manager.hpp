#pragma once

#include <logos/wallet_server/client/callback_handler.hpp>
#include <logos/wallet_server/client/common.hpp>

#include <mutex>

class CallbackManager
{
    using Handlers = wallet_server::client::callback::Handlers;
    using Handle   = wallet_server::client::callback::Handle;

public:

    void OnCallbackDone(const Handle handle);

protected:

    std::mutex _mutex;
    Handlers   _handlers;
};
