#pragma once
#include <logos/blockstore.hpp>
#include <logos/ledger.hpp>
#include <logos/node/wallet.hpp>

namespace rpclogic
{
    using BlockStore = logos::block_store;
    using BoostJson = boost::property_tree::ptree;

    template<typename T>
    struct RpcResponse
    {
        T contents;
        bool error;
        std::string error_msg;
    };
    RpcResponse<BoostJson> tokens_info(const BoostJson& request, BlockStore& store);

    RpcResponse<BoostJson> account_info(const BoostJson& request, BlockStore& store);

    RpcResponse<BoostJson> account_balance(const BoostJson& request, BlockStore& store);


}
