#include <gtest/gtest.h>
#include <logos/node/rpc_logic.hpp>
#include <logos/unit_test/msg_validator_setup.hpp>


#define Unit_Test_Request_Serialization

#ifdef Unit_Test_Request_Serialization


TEST(RPC, tokens_info)
{

    auto get_tree = [](char const * json)
    {
        boost::iostreams::array_source as(json, strlen(json));
        boost::iostreams::stream<boost::iostreams::array_source> is(as);

        boost::property_tree::ptree tree;
        boost::property_tree::read_json(is, tree);

        return tree;
    };
    using BoostJson = boost::property_tree::ptree;
    logos::block_store* store(get_db());
    store->clear(store->account_db);
    {
        TokenAccount token_account(0,10,0,100,25,1,7,11);
        token_account.fee_type = TokenFeeType::Flat;
        token_account.fee_rate = 7;
        token_account.symbol = "FOO";
        token_account.name = "foocoin";
        token_account.issuer_info = "issuer string";
        AccountAddress address = 1234567;
        std::string account_str = address.to_string();
        AccountAddress address2(account_str);
        ASSERT_EQ(address,address2);


        //database consistency check
        //make sure the txn is destroyed, because RpcLogic makes its own txn
        {
            logos::transaction txn(store->environment,nullptr,true);
            bool error = store->token_account_put(address,token_account,txn);
            ASSERT_FALSE(error);
        }
        TokenAccount account_info2;
        bool error = store->token_account_get(address,account_info2);
        ASSERT_FALSE(error);
        ASSERT_EQ(token_account,account_info2);

        //single token request
        BoostJson request;
        BoostJson tokens;
        BoostJson account_json;
        account_json.put("",account_str);
        tokens.push_back(std::make_pair("",account_json));
        request.add_child("tokens",tokens);

        auto res = rpclogic::tokens_info(request,*store);
        ASSERT_FALSE(res.error);
        //std::stringstream ss;
        //boost::property_tree::json_parser::write_json(ss, request);

        //std::cout << ss.str() << std::endl;

        auto child = res.contents.get_child(account_str);
        ASSERT_EQ(
                child.get<std::string>("token_balance"),
                token_account.token_balance.to_string_dec());

        ASSERT_EQ(
                child.get<std::string>("token_fee_balance"),
                token_account.token_fee_balance.to_string_dec());

        ASSERT_EQ(child.get<std::string>("symbol"),token_account.symbol);
        ASSERT_EQ(child.get<std::string>("name"),token_account.name);
        ASSERT_EQ(child.get<std::string>("issuer_info"),
                token_account.issuer_info);


    }

    //multiple token requests
    std::vector<std::pair<AccountAddress,TokenAccount>> accounts;
    {
        logos::transaction txn(store->environment,nullptr,true);
        size_t num_tokens = 10;
        for(size_t i = 0; i < num_tokens; ++i)
        {
            TokenAccount token_account(0,10,0,100,25,1,7,11);
            token_account.fee_type = TokenFeeType::Flat;
            token_account.fee_rate = 7;
            token_account.symbol = "FOO" + std::to_string(i);
            token_account.name = "foocoin" + std::to_string(i);
            token_account.issuer_info = "issuer string" + std::to_string(i);
            AccountAddress address = i * 1234567;
            bool error = store->token_account_put(address,token_account,txn);
            ASSERT_FALSE(error);
            accounts.push_back(
                    std::make_pair(
                        address,token_account));
        }
    }
    {

        BoostJson request;
        BoostJson tokens;
        for(size_t i = 0; i < accounts.size(); ++i)
        {
            BoostJson account_json;
            account_json.put("",accounts[i].first.to_string());
            tokens.push_back(std::make_pair("",account_json));
        }
        request.add_child("tokens",tokens);
        auto res = rpclogic::tokens_info(request,*store);
        ASSERT_FALSE(res.error);

        for(size_t i = 0; i < accounts.size(); ++i)
        {
            auto child = res.contents.get_child(accounts[i].first.to_string());
            ASSERT_EQ(
                    child.get<std::string>("token_balance"),
                    accounts[i].second.token_balance.to_string_dec());

            ASSERT_EQ(
                    child.get<std::string>("token_fee_balance"),
                    accounts[i].second.token_fee_balance.to_string_dec());
            ASSERT_EQ(
                    child.get<std::string>("symbol"),
                    accounts[i].second.symbol);
            ASSERT_EQ(
                    child.get<std::string>("name"),
                    accounts[i].second.name);
            ASSERT_EQ(
                    child.get<std::string>("issuer_info"),
                    accounts[i].second.issuer_info);
        }
    }


    //with details
    {

        TokenAccount token_account(0,10,0,100,25,1,7,11);
        token_account.fee_type = TokenFeeType::Flat;
        token_account.fee_rate = 3;
        token_account.symbol = "BAR";
        token_account.name = "barcoin";
        token_account.issuer_info = "random message";
        AccountAddress address = 42;

        {
            logos::transaction txn(store->environment,nullptr,true);
            bool error = store->token_account_put(address,token_account,txn);
            ASSERT_FALSE(error);
        }

        BoostJson request;
        BoostJson tokens;
        BoostJson account;
        account.put("",address.to_string());
        tokens.push_back(std::make_pair("",account));
        request.add_child("tokens",tokens);
        request.put("details","true");
        auto res = rpclogic::tokens_info(request,*store);


        ASSERT_FALSE(res.error);
        auto child = res.contents.get_child(address.to_string());
        ASSERT_FALSE(child.get_child("settings").empty());
        ASSERT_TRUE(child.get_child("controllers").empty());

        auto settings = child.get_child("settings");
        ASSERT_EQ(settings.size(), token_account.settings.field.size());


        for(auto& s : settings)
        {
            ASSERT_EQ(s.second.get_value<std::string>(),"false");
        }

        token_account.Set(TokenSetting::AddTokens,true);
        token_account.Set(TokenSetting::Revoke,true);
        {
            logos::transaction txn(store->environment,nullptr,true);
            bool error = store->token_account_put(address,token_account,txn);
            ASSERT_FALSE(error);
        }

        res = rpclogic::tokens_info(request,*store);
        child = res.contents.get_child(address.to_string());
        settings = child.get_child("settings");
        ASSERT_EQ(settings.size(), token_account.settings.field.size());

        ASSERT_EQ(settings.get<std::string>(
                    GetTokenSettingField(TokenSetting::AddTokens)),
                "true");
        ASSERT_EQ(
                settings.get<std::string>(
                    GetTokenSettingField(TokenSetting::Revoke)),
                "true");

        ASSERT_EQ(
                settings.get<std::string>(
                    GetTokenSettingField(TokenSetting::Freeze)),
                "false");

        ControllerInfo controller;
        controller.account = 123;
        auto ptoi = [](ControllerPrivilege p)
        {
            return static_cast<
                std::underlying_type<ControllerPrivilege>::type
                >(p);
        };
        controller.privileges.Set(ptoi(ControllerPrivilege::AddTokens),true);
        controller.privileges.Set(ptoi(ControllerPrivilege::Freeze),true);
        token_account.controllers.push_back(controller);

        ControllerInfo controller2;
        controller2.account = 456;

        controller2.privileges.Set(ptoi(ControllerPrivilege::Burn),true);
        token_account.controllers.push_back(controller2);

        {
            logos::transaction txn(store->environment,nullptr,true);
            bool error = store->token_account_put(address,token_account,txn);
            ASSERT_FALSE(error);
        }

        res = rpclogic::tokens_info(request,*store);
        child = res.contents.get_child(address.to_string());
        auto controllers = child.get_child("controllers");
        ASSERT_FALSE(controllers.empty());
        ASSERT_EQ(controllers.size(),2);
        std::stringstream ss;
        boost::property_tree::json_parser::write_json(ss, controllers);

        std::cout << ss.str() << std::endl;
        std::map<std::string,std::vector<std::string>> account_priv_map;

        for(auto& c : controllers)
        {
            std::string lgs_account = c.second.get<std::string>("account");
            auto privileges = c.second.get_child("privileges");
            std::vector<std::string> priv_str;
            for(auto& p : privileges)
            {
                priv_str.push_back(p.second.get_value<std::string>());
            }
            sort(priv_str.begin(),priv_str.end());
            account_priv_map[lgs_account] = priv_str;
        }

        {
            using namespace::request::fields;
            std::vector<std::string> priv_str_exp{ADD,FREEZE};
            ASSERT_EQ(
                    account_priv_map[controller.account.to_account()]
                    ,priv_str_exp);
            std::vector<std::string> priv_str_exp2{BURN};
            ASSERT_EQ(
                    account_priv_map[controller2.account.to_account()]
                    ,priv_str_exp2);
        }

    }
}

void setupAccountWithTokens(
        AccountAddress& address,
        logos::account_info& account,
        std::vector<TokenEntry>& entries,
        size_t& num_tokens)
{
    logos::block_store* store(get_db());
    address = 42;
    account.head = 23;
    account.balance = 100;
    account.block_count = 20;
    account.modified = 12345;
    account.rep_block = 12;
    account.open_block = 2;
    account.receive_head = 13;
    account.receive_count = 10;

    num_tokens = 10;
    entries.clear();
    for(size_t i = 0; i < num_tokens; ++i)
    {
        TokenEntry entry;
        entry.token_id = i;
        entry.balance = 100 + i;
        entry.status.frozen = false;
        entry.status.whitelisted = true;
        entries.push_back(entry);
    }

    account.entries = entries;

    {
        logos::transaction txn(store->environment,nullptr,true);
        bool res =store->account_put(address,account,txn);
        ASSERT_FALSE(res);
        logos::account_info account2;
        res = store->account_get(address,account2,txn);
        ASSERT_FALSE(res);
        ASSERT_EQ(account,account2);
    }
}

TEST(RPC, block)
{
    using BoostJson = boost::property_tree::ptree;
    logos::block_store* store(get_db());
    store->clear(store->state_db);

    TokenSend token_req;
    token_req.type = RequestType::SendTokens;
    token_req.token_fee = 5;
    std::vector<Transaction<Amount>> transactions;
    for(size_t i = 0; i < 3; ++i)
    {
        transactions.emplace_back(i,i*100);
    }
    token_req.transactions = transactions;
    token_req.Hash();
    {
        logos::transaction txn(store->environment,nullptr,true);
        bool error = store->request_put(token_req,txn);
        ASSERT_FALSE(error);
        std::shared_ptr<Request> request_ptr;
        error = store->request_get(token_req.Hash(),request_ptr,txn);
        ASSERT_FALSE(error);
    }

    BoostJson request;
    request.put("hash",token_req.Hash().to_string());

    auto res = rpclogic::block(request,*store);
    std::cout << "returned from rpclogic::block" << std::endl;
    ASSERT_FALSE(res.error);
    bool error = false;
    TokenSend token_req_2(error,res.contents);
    ASSERT_FALSE(error);
    ASSERT_EQ(token_req_2.transactions,token_req.transactions);
    ASSERT_EQ(token_req_2.token_fee,token_req.token_fee);
}

TEST(RPC, blocks)
{
    using BoostJson = boost::property_tree::ptree;
    logos::block_store* store(get_db());
    store->clear(store->state_db);

    std::unordered_map<BlockHash, TokenSend> token_reqs;
    BoostJson request;
    BoostJson hashes;
    for(size_t i = 0; i < 5; ++i)
    {

        TokenSend token_req;
        token_req.type = RequestType::SendTokens;
        token_req.token_fee = 5;
        std::vector<Transaction<Amount>> transactions;
        for(size_t j = 0; j < 3; ++j)
        {
            transactions.emplace_back(i*j,i*j*100);
        }
        token_req.transactions = transactions;
        token_req.Hash();
        {
            logos::transaction txn(store->environment,nullptr,true);
            bool error = store->request_put(token_req,txn);
            ASSERT_FALSE(error);
            std::shared_ptr<Request> request_ptr;
            error = store->request_get(token_req.Hash(),request_ptr,txn);
            ASSERT_FALSE(error);
        }
        token_reqs[token_req.Hash()] = token_req;
        BoostJson token_hash;
        token_hash.put("",token_req.Hash().to_string());
        hashes.push_back(std::make_pair("",token_hash));
    }


    request.add_child("hashes",hashes);



    auto res = rpclogic::blocks(request,*store);
    ASSERT_FALSE(res.error);
    bool error = false;
    for(auto it : res.contents.get_child("blocks"))
    {
        bool error = false;
        TokenSend token_req(error,it.second);
        ASSERT_FALSE(error);
        BlockHash hash(it.first);
        ASSERT_EQ(token_reqs[hash].transactions,token_req.transactions);
        ASSERT_EQ(token_reqs[hash].token_fee,token_req.token_fee);
    }

}

TEST(RPC, account_info)
{

    using BoostJson = boost::property_tree::ptree;

    logos::block_store* store(get_db());
    store->clear(store->account_db);
    auto print = [](BoostJson res){
        std::stringstream ss;
        boost::property_tree::json_parser::write_json(ss, res);
        std::cout << ss.str() << std::endl;
    };
    logos::account_info account;
    AccountAddress address;
    std::vector<TokenEntry> entries;
    size_t num_tokens;
    setupAccountWithTokens(address,account,entries,num_tokens);

    BoostJson request;
    request.put("account",address.to_account());
    auto res = rpclogic::account_info(request, *store);
    ASSERT_FALSE(res.error);

    auto get = [](BoostJson res,const std::string& path)
    {
        return res.get_child(path).get_value<std::string>();
    };
    ASSERT_EQ(get(res.contents,"balance"),"100");
    ASSERT_EQ(get(res.contents,"frontier"),account.head.to_string());
    ASSERT_EQ(get(res.contents,"receive_tip"),account.receive_head.to_string());
    ASSERT_EQ(get(res.contents,"open_block"),account.open_block.to_string());
    ASSERT_EQ(get(res.contents,"representative_block"),
            account.rep_block.to_string());
    ASSERT_EQ(get(res.contents,"modified_timestamp"),
            std::to_string(account.modified));
    ASSERT_EQ(
            get(res.contents,"request_count"),
            std::to_string(account.block_count + account.receive_count));


    ASSERT_FALSE(res.error);
    ASSERT_EQ(res.contents.get_child("tokens").size(),num_tokens);

    for(auto& e : entries)
    {
        ASSERT_EQ(
                get(res.contents,"tokens."+e.token_id.to_string()+".frozen"),
                "false");

        ASSERT_EQ(
                get(res.contents,
                    "tokens."+e.token_id.to_string()+".whitelisted"),
                "true");

        ASSERT_EQ(
                get(res.contents,"tokens."+e.token_id.to_string()+".balance"),
                e.balance.to_string_dec());
    }

    request.put("tokens","");

    res = rpclogic::account_info(request,*store);

    ASSERT_EQ(res.contents.find("tokens"),res.contents.not_found());

    request.erase("tokens");
    BoostJson tokens_request;

    BoostJson token;
    token.put("",entries[0].token_id.to_string());
    BoostJson token2;
    token2.put("",entries[1].token_id.to_string());
    tokens_request.push_back(std::make_pair("",token));
    tokens_request.push_back(std::make_pair("",token2));

    request.add_child("tokens",tokens_request);
    print(request);

    res = rpclogic::account_info(request,*store);

    ASSERT_FALSE(res.error);
    ASSERT_EQ(res.contents.get_child("tokens").size(),2);
    ASSERT_EQ(
            get(res.contents,
                "tokens."+entries[0].token_id.to_string()+".frozen"),
            "false");

    ASSERT_EQ(
            get(res.contents,
                "tokens."+entries[0].token_id.to_string()+".whitelisted"),
            "true");

    ASSERT_EQ(
            get(res.contents,
                "tokens."+entries[0].token_id.to_string()+".balance"),
            entries[0].balance.to_string_dec());

    ASSERT_EQ(
            get(res.contents,
                "tokens."+entries[1].token_id.to_string()+".frozen"),
            "false");

    ASSERT_EQ(
            get(res.contents,
                "tokens."+entries[1].token_id.to_string()+".whitelisted"),
            "true");

    ASSERT_EQ(
            get(res.contents,
                "tokens."+entries[1].token_id.to_string()+".balance"),
            entries[1].balance.to_string_dec());

}


TEST(RPC, account_balance)
{
    using BoostJson = boost::property_tree::ptree;
    logos::block_store* store(get_db());
    store->clear(store->account_db);
    auto print = [](BoostJson res){
        std::stringstream ss;
        boost::property_tree::json_parser::write_json(ss, res);
        std::cout << ss.str() << std::endl;
    };
    auto get = [](BoostJson res,const std::string& path)
    {
        return res.get_child(path).get_value<std::string>();
    };

    logos::account_info account;
    AccountAddress address;
    std::vector<TokenEntry> entries;
    size_t num_tokens;
    setupAccountWithTokens(address,account,entries,num_tokens);

    BoostJson request;
    request.put("account",address.to_account());
    auto res = rpclogic::account_balance(request, *store);
    ASSERT_FALSE(res.error);
    ASSERT_EQ(get(res.contents,"balance"),"100");

    ASSERT_EQ(res.contents.get_child("token_balances").size(),num_tokens);

    print(res.contents);

    for(auto& e : entries)
    {

        ASSERT_EQ(
                get(res.contents,"token_balances."+e.token_id.to_string()),
                e.balance.to_string_dec());
    }

    request.put("tokens","");

    res = rpclogic::account_balance(request,*store);

    ASSERT_EQ(res.contents.find("token_balances"),res.contents.not_found());
    request.erase("tokens");
    BoostJson tokens_request;

    BoostJson token;
    token.put("",entries[0].token_id.to_string());
    BoostJson token2;
    token2.put("",entries[1].token_id.to_string());
    tokens_request.push_back(std::make_pair("",token));
    tokens_request.push_back(std::make_pair("",token2));

    request.add_child("tokens",tokens_request);

    res = rpclogic::account_balance(request,*store);

    ASSERT_FALSE(res.error);
    ASSERT_EQ(res.contents.get_child("token_balances").size(),2);

    print(res.contents);

    ASSERT_EQ(
            get(res.contents,"token_balances."+entries[0].token_id.to_string()),
            entries[0].balance.to_string_dec());


    ASSERT_EQ(
            get(res.contents,"token_balances."+entries[1].token_id.to_string()),
            entries[1].balance.to_string_dec());
}
#endif
