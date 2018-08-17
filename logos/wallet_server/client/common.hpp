#pragma once

#include <unordered_map>

class CallbackHandler;

namespace wallet_server {
namespace client        {
namespace callback      {

    using Handlers = std::unordered_map<uint64_t, CallbackHandler>;
    using Handle   = uint64_t;

} // namespace callback
} // namespace client
} // namespace wallet_server
