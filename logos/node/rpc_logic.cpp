#include <logos/node/rpc_logic.hpp>
#include <unordered_set>

namespace rpclogic
{
RpcResponse<BoostJson> tokens_info(
        const BoostJson& request,
        BlockStore& store)
{
    RpcResponse<BoostJson> res;
    res.error = false;
    try{
        boost::property_tree::ptree response;
        bool details = "true" == request.get<std::string>("details","false");
        for(auto & item : request.get_child("tokens"))
        {
            std::string account_string(item.second.get_value<std::string>());
            logos::uint256_union account(account_string);
            TokenAccount token_account_info;
            if(!store.token_account_get(account,token_account_info))
            {
                response.add_child(
                        account_string,
                        token_account_info.SerializeJson(details));
            } else
            {
                res.error = true;
            }
        }
        res.contents = response;
    }
    catch(std::exception& e)
    {
        res.error = true;
        res.error_msg = e.what();
    }
    return res;
}




RpcResponse<BoostJson> account_info(
        const BoostJson& request,
        BlockStore& store)
{
    RpcResponse<BoostJson> res;
    res.error = false;
    try {
        std::string account_text (request.get<std::string> ("account"));
        logos::uint256_union account;
        res.error = account.decode_account (account_text);
        if (!res.error)
        {
            const bool representative =
                request.get<bool>("representative", false);
            const bool weight = request.get<bool>("weight", false);
            logos::transaction transaction (store.environment, nullptr, false);
            logos::account_info info;

            MDB_dbi db = store.account_db;
            res.error = store.account_get (transaction, account, info, db);
            if (!res.error)
            {
                boost::property_tree::ptree response;
                response.put ("frontier", info.head.to_string ());
                response.put ("receive_tip", info.receive_head.to_string ());
                response.put ("open_block", info.open_block.to_string ());
                response.put ("representative_block",
                        info.rep_block.to_string ());
                std::string balance;
                logos::uint128_union (info.balance).encode_dec (balance);
                response.put ("balance", balance);
                response.put ("modified_timestamp",
                        std::to_string (info.modified));
                response.put ("request_count",
                        std::to_string(info.block_count + info.receive_count));

                std::unordered_set<std::string> token_ids;
                BoostJson token_tree;
                bool nofilter = true;
                if(request.find("tokens")!=request.not_found())
                {
                    nofilter = false;
                    for(auto& t : request.get_child("tokens"))
                    {
                        token_ids.emplace(t.second.data());
                    }

                }
                for(TokenEntry& e : info.entries)
                {
                    auto token_id_str = e.token_id.to_string();
                    if(nofilter ||
                            token_ids.find(e.token_id.to_string())
                            !=token_ids.end())
                    {
                        boost::property_tree::ptree entry_tree;
                        entry_tree.put("whitelisted",e.status.whitelisted);
                        entry_tree.put("frozen",e.status.frozen);
                        entry_tree.put("balance",e.balance.to_string_dec());
                        token_tree.add_child(e.token_id.to_string(),entry_tree);
                    }
                }
                if(token_tree.size() > 0)
                {
                    response.add_child("tokens",token_tree);
                }

                res.contents = response;
            }
            else
            {
                res.error_msg = "Account not found";
            }
        }
        else
        {
            res.error_msg = "Bad account number";
        }
    } catch(std::exception& e)
    {
        res.error = true;
        res.error_msg = e.what();
    }
    return res;
}

RpcResponse<BoostJson> account_balance(
        const BoostJson& request,
        BlockStore& store)
{

    RpcResponse<BoostJson> res;
    res.error = false;
    try {
        BoostJson response;

        std::string account_text (request.get<std::string> ("account"));
        logos::uint256_union account;
        res.error = account.decode_account(account_text);
        if(res.error)
        {
            res.error_msg = "failed to decode account: " + account_text;
            return res;
        }

        logos::transaction txn (store.environment, nullptr, false);
        logos::account_info account_info;
        res.error = store.account_get(account, account_info,txn);
        if(res.error)
        {
            res.error_msg = "failed to get account from db: "
                + account.to_string();
        }
        std::string balance_str;
        account_info.balance.encode_dec(balance_str);
        response.put("balance",balance_str);

        std::unordered_set<std::string> token_ids;
        BoostJson token_tree;
        bool nofilter = true;
        if(request.find("tokens")!=request.not_found())
        {
            nofilter = false;
            for(auto& t : request.get_child("tokens"))
            {
                token_ids.emplace(t.second.data());
            }

        }
        for(TokenEntry& e : account_info.entries)
        {
            if(nofilter ||
                    token_ids.find(e.token_id.to_string())!=token_ids.end())
            {
                token_tree.put(e.token_id.to_string(),e.balance.to_string_dec());
            }
        }
        if(token_tree.size() > 0)
        {
            response.add_child("token_balances",token_tree);
        }

        res.contents = response;
    }
    catch(std::exception& e)
    {
        res.error = true;
        res.error_msg = e.what();
    }
    return res;
}

BoostJson getBlockJson(const logos::uint256_union& hash, BlockStore& store)
{
    logos::transaction transaction (store.environment, nullptr, false);
    std::shared_ptr<Request> request_ptr;
    ReceiveBlock receive;
    if (!store.request_get(hash, request_ptr, transaction))
    {
        return request_ptr->SerializeJson();
    }
    else if (!store.receive_get(hash, receive, transaction))
    {
        return receive.SerializeJson();
    }
    else
    {
        BoostJson empty_tree;
        return empty_tree;
    }
}

RpcResponse<BoostJson> block(const BoostJson& request, BlockStore& store)
{
    RpcResponse<BoostJson> res;
    res.error = false;
    try
    {
        std::string hash_text (request.get<std::string> ("hash"));
        logos::uint256_union hash;
        if (hash.decode_hex (hash_text))
        {
            res.error = true;
            res.error_msg = "Bad hash number";
            return res;
        }
        res.contents = getBlockJson(hash,store);
        if(res.contents.empty())
        {
            res.error = true;
            res.error_msg = "block not found: " + hash_text;
        }
    }
    catch(std::exception& e)
    {
        res.error = true;
        res.error_msg = e.what();
    }
    return res;
}

RpcResponse<BoostJson> blocks(const BoostJson& request, BlockStore& store)
{
    RpcResponse<BoostJson> res;
    res.error = false;
    try
    {
        std::vector<std::string> hashes;
        boost::property_tree::ptree blocks;
        logos::transaction transaction (store.environment, nullptr, false);
        for (const boost::property_tree::ptree::value_type & hashes : request.get_child ("hashes"))
        {
            std::string hash_text = hashes.second.data ();
            logos::uint256_union hash;
            if (hash.decode_hex (hash_text))
            {
                res.error = true;
                res.error_msg += "Bad hash number: " + hash_text + " .";
            }
            else
            {
                boost::property_tree::ptree contents = getBlockJson(hash,store);
                if(contents.empty())
                {
                    res.error = true;
                    res.error_msg += "Block not found: " + hash_text + " .";
                }
                else
                {
                    blocks.push_back (std::make_pair(hash_text, contents));
                }
            }
        }
        res.contents.add_child ("blocks", blocks);
    }
    catch(std::exception& e)
    {
        res.error = true;
        res.error_msg = e.what();
    }
    return res;
}



}
