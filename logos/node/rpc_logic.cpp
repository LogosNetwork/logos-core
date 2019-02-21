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


RpcResponse<BoostJson> token_list(
        const BoostJson& request,
        BlockStore& store)
{
    RpcResponse<BoostJson> res;
    res.error = false;
    try{

        bool details = "true" == request.get<std::string>("details","false");
        //Default to giving only 10 tokens if count is not specified
        int count = std::stol(request.get<std::string>("count","10"));
        if(count < 0)
        {
            res.error = true;
            res.error_msg = "Count must be greater than 0";
            return res;
        }

        logos::transaction txn(store.environment, nullptr,false);
        std::string head_str = request.get<std::string>("head","");
        logos::store_iterator it(nullptr);
        if(head_str != "")
        {
            logos::uint256_union head(head_str);
            it = logos::store_iterator(
                    txn,
                    store.token_account_db,
                    logos::mdb_val(head));
            ++it;
        } else
        {
            it = logos::store_iterator(txn, store.token_account_db);
        }

        std::string last = "";
        boost::property_tree::ptree response;
        for(size_t i = 0;
                i < count && it != logos::store_iterator(nullptr);
                ++i,++it)
        {
            bool error;
            TokenAccount token_account_info(error, it->second);
            if(!error)
            {
                response.add_child(
                        it->first.uint256().to_string(),
                        token_account_info.SerializeJson(details));
                last = it->first.uint256().to_string();
            }
            else
            {
                res.error = true;
                res.error_msg = "Error deserializing TokenAccount. Key : "
                    + it->first.uint256().to_string();
            }
        }
        if(it == logos::store_iterator(nullptr))
        {
            response.put("last","null");
        }
        else
        {
            response.put("last",last);
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
                        entry_tree.put("balance",e.balance);
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
                token_tree.put(e.token_id.to_string(),e.balance);
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
}
