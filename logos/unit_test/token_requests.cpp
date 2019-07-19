#include <gtest/gtest.h>

#include <logos/consensus/persistence/request/request_persistence.hpp>
#include <logos/consensus/persistence/reservations.hpp>
#include <logos/unit_test/msg_validator_setup.hpp>
#include <logos/token/requests.hpp>

#define Unit_Test_Token_Requests
#ifdef  Unit_Test_Token_Requests

TEST (Token_Requests, Request_Flow_1)
{
    auto * store = get_db();
    clear_dbs();

    auto reservations = std::make_shared<ConsensusReservations>(*store);
    PersistenceManager<R> persistence(*store, reservations);

    auto get_tree = [](char const * json)
    {
        boost::iostreams::array_source as(json, strlen(json));
        boost::iostreams::stream<boost::iostreams::array_source> is(as);

        boost::property_tree::ptree tree;
        boost::property_tree::read_json(is, tree);

        return tree;
    };

    auto apply_request = [&persistence, store](auto request_ptr)
    {
        logos::transaction transaction(store->environment, nullptr, true);

        store->request_put(*request_ptr, transaction);

        persistence.ApplyRequest(request_ptr, 0, 0, transaction);
        persistence.Release(request_ptr);
    };

    auto send_request = [&persistence, store, &apply_request](auto request_ptr,
                                                              auto & message,
                                                              auto expected_result)
    {
        std::cout << std::endl
                  << "----------------------------------------------------------"
                  << std::endl
                  << message
                  << std::endl;

        logos::process_return result;
        persistence.ValidateAndUpdate(request_ptr, 0, result, false);

        ASSERT_EQ(result.code, expected_result);

        std::cout << "Result: " << ProcessResultToString(result.code) << std::endl;
        std::cout << "PASS" << std::endl;

        if(expected_result == logos::process_result::progress)
        {
            apply_request(request_ptr);
        }
    };

    bool error = false;

    using logos::amount;
    using logos::raw_key;
    using logos::account_info;
    using logos::process_result;

    raw_key genesis_key;
    error = genesis_key.data.decode_hex("34F0A37AAD20F4A260F0A5B3CB3D7FB50673212263E58A380BC10474BB039CE4");
    assert(!error);

    raw_key issuer_key;
    error = issuer_key.data.decode_hex("B3B4FC6657453C1DD58B3E14D2DB9F19C8D8F82BBBD5A718DC2E31BDA3B6B885");
    assert(!error);

    raw_key controller_1_key;
    error = controller_1_key.data.decode_hex("3C2D93DE46093DEBD2E82C18C66FD09618D032472DE8D5213A1FEAE1FD6F420F");
    assert(!error);

    raw_key controller_2_key;
    error = controller_2_key.data.decode_hex("B5A99B6038978689A693890A8F05766E46483409A4F6453DCE793E3F80AAE7B2");
    assert(!error);

    raw_key user_1_key;
    error = user_1_key.data.decode_hex("2786FEEF19046EF706309D32D24D912C0426E1E358A689A323FEF29E70BE3F90");
    assert(!error);

    raw_key user_2_key;
    error = user_2_key.data.decode_hex("D3FC7B4515D4EFB7547B7B8198065304070BD615D947CDFDDF11225A6FF9E255");
    assert(!error);

    AccountAddress genesis;
    error = genesis.decode_account("lgs_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo");
    assert(!error);

    AccountAddress issuer;
    error = issuer.decode_account("lgs_1sibjaeaceh59dh7fefo49narpsoytqac5hafhujum3grnd7qrhbczfy9wx8");
    assert(!error);

    AccountAddress controller_1;
    error = controller_1.decode_account("lgs_15p6h3z7dgif1kt8skmdmo8xmobh3xyfzthoden6jqu34t6i4sgtcr4pfj5h");
    assert(!error);

    AccountAddress controller_2;
    error = controller_2.decode_account("lgs_1mkqajo9pedc1x764b5y5yzkykcm3h3hx1bumznzhgjqimjpajy9w5qfsis6");
    assert(!error);

    AccountAddress user_1;
    error = user_1.decode_account("lgs_1gwfynd84gan8i4rpzzxkikbz7158wha96qpni38rj31hd3dcbrwscey8ahy");
    assert(!error);

    AccountAddress user_2;
    error = user_2.decode_account("lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    assert(!error);

    char const * create_accounts = R"%%%({
        "type": "send",
        "origin": "lgs_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000000000000000000000",
        "sequence": "0",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "transactions": [
            {
                 "destination": "lgs_1sibjaeaceh59dh7fefo49narpsoytqac5hafhujum3grnd7qrhbczfy9wx8",
                 "amount": "100000000000000000000000000"
            },
            {
                 "destination": "lgs_15p6h3z7dgif1kt8skmdmo8xmobh3xyfzthoden6jqu34t6i4sgtcr4pfj5h",
                 "amount": "10000000000000000000000000"
            },
            {
                 "destination": "lgs_1mkqajo9pedc1x764b5y5yzkykcm3h3hx1bumznzhgjqimjpajy9w5qfsis6",
                 "amount": "10000000000000000000000000"
            },
            {
                 "destination": "lgs_1gwfynd84gan8i4rpzzxkikbz7158wha96qpni38rj31hd3dcbrwscey8ahy",
                 "amount": "10000000000000000000000000"
            },
            {
                 "destination": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
                 "amount": "10000000000000000000000000"
            }
        ],
        "work": "0"
     })%%%";

    auto tree = get_tree(create_accounts);
    error = false;
    Send send(error, tree);
    assert(!error);

    send.Sign(genesis_key.data);

    {
        amount genesis_balance;
        error = genesis_balance.decode_dec("10000000000000000000000000000");
        assert(!error);

        logos::transaction transaction(store->environment, nullptr, true);

        account_info genesis_info;
        genesis_info.SetBalance(genesis_balance, 0, transaction);

        account_info blank;

        store->account_put(genesis, genesis_info, transaction);
        store->account_put(issuer, blank, transaction);
        store->account_put(controller_1, blank, transaction);
        store->account_put(controller_2, blank, transaction);
        store->account_put(user_1, blank, transaction);
        store->account_put(user_2, blank, transaction);
    }

    // Create accounts
    //
    //
    send_request(std::make_shared<Send>(send),
                 "Creating accounts",
                 process_result::progress);

    std::cout << "Waiting for accounts to be created..." << std::endl;
    while(!store->account_exists(issuer))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    while(!store->account_exists(controller_1))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    while(!store->account_exists(controller_2))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "Done" << std::endl;

    char const * token_issuance = R"%%%({
        "type": "issuance",
        "origin": "lgs_1sibjaeaceh59dh7fefo49narpsoytqac5hafhujum3grnd7qrhbczfy9wx8",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000000000000000000000000",
        "sequence": "0",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "symbol": "MYC",
        "name": "MyCoin",
        "total_supply": "6000000000000",
        "fee_type": "flat",
        "fee_rate": "20000",
        "settings": [
            "issuance",
            "revoke",
            "modify_issuance",
            "whitelist",
            "modify_whitelist",
            "modify_adjust_fee",
            "modify_freeze"
        ],
        "controllers": [
            {
                "account": "lgs_15p6h3z7dgif1kt8skmdmo8xmobh3xyfzthoden6jqu34t6i4sgtcr4pfj5h",
                "privileges": [
                    "change_issuance",
                    "change_revoke",
                    "issuance",
                    "distribute",
                    "burn"
                ]
            },
            {
                "account": "lgs_1mkqajo9pedc1x764b5y5yzkykcm3h3hx1bumznzhgjqimjpajy9w5qfsis6",
                "privileges": [
                    "change_issuance",
                    "change_revoke",
                    "change_freeze",
                    "revoke",
                    "withdraw_fee",
                    "withdraw_logos",
                    "adjust_fee",
                    "change_adjust_fee",
                    "update_controller",
                    "update_issuer_info",
                    "freeze",
                    "whitelist"
                ]
            }
        ],
        "issuer_info": "MyCoin is a coin owned by me."
     })%%%";

    tree = get_tree(token_issuance);
    error = false;
    Issuance issuance(error, tree);
    assert(!error);

    issuance.token_id = GetTokenID(issuance);
    issuance.Sign(issuer_key.data);

    // Issue tokens
    //
    //
    send_request(std::make_shared<Issuance>(issuance),
                 "Issueing tokens",
                 process_result::progress);

    std::cout << "Waiting for token account to be created..." << std::endl;

    TokenAccount account;
    while(store->token_account_get(issuance.token_id, account))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    account_info issuer_account_info;
    error = store->account_get(issuer, issuer_account_info);
    assert(!error);

    char const * token_issue_adtl = R"%%%({
        "type": "issue_additional",
        "origin": "lgs_15p6h3z7dgif1kt8skmdmo8xmobh3xyfzthoden6jqu34t6i4sgtcr4pfj5h",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000000000000000000000",
        "sequence": "0",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "CE4F3A844DF04A49B78E1BCF47F4E6300D83253DFC17C084ED9C0B598F19D979",
        "amount": "20000"
     })%%%";

    tree = get_tree(token_issue_adtl);
    error = false;
    IssueAdditional issue_adtl(error, tree);
    assert(!error);

    issue_adtl.Sign(controller_1_key.data);

    send_request(std::make_shared<IssueAdditional>(issue_adtl),
                 "Issueing additional tokens",
                 process_result::progress);

    std::cout << "Waiting for token account balance to update..." << std::endl;
    while(true)
    {
        store->token_account_get(issuance.token_id, account);
        if(account.token_balance != Amount{6000000020000})
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done -  balance: "
                      << account.token_balance.to_string_dec()
                      << std::endl;
            break;
        }
    }

    char const * token_change_setting = R"%%%({
        "type": "change_setting",
        "origin": "lgs_15p6h3z7dgif1kt8skmdmo8xmobh3xyfzthoden6jqu34t6i4sgtcr4pfj5h",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000000000000000000000",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "CE4F3A844DF04A49B78E1BCF47F4E6300D83253DFC17C084ED9C0B598F19D979",
        "setting": "adjust_fee",
        "value": "true"
     })%%%";

    tree = get_tree(token_change_setting);
    ChangeSetting change_setting(error, tree);
    assert(!error);

    change_setting.previous = issue_adtl.GetHash();
    change_setting.Sign(controller_1_key.data);

    send_request(std::make_shared<ChangeSetting>(change_setting),
                 "Changing fee setting without authorization",
                 process_result::unauthorized_request);

    change_setting.origin = controller_2;
    change_setting.Sign(controller_2_key.data);

    send_request(std::make_shared<ChangeSetting>(change_setting),
                 "Changing fee",
                 process_result::progress);

    std::cout << "Waiting for token account fee setting to change..." << std::endl;
    while(true)
    {
        store->token_account_get(issuance.token_id, account);
        if(!account.Allowed(TokenSetting::AdjustFee))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done" << std::endl;
            break;
        }
    }

    char const * token_immute_setting = R"%%%({
        "type": "immute_setting",
        "origin": "lgs_15p6h3z7dgif1kt8skmdmo8xmobh3xyfzthoden6jqu34t6i4sgtcr4pfj5h",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000000000000000000000",
        "sequence": "2",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "CE4F3A844DF04A49B78E1BCF47F4E6300D83253DFC17C084ED9C0B598F19D979",
        "setting": "adjust_fee"
     })%%%";

    tree = get_tree(token_immute_setting);
    ImmuteSetting immute(error, tree);
    assert(!error);

    immute.previous = change_setting.GetHash();
    immute.Sign(controller_1_key.data);

    send_request(std::make_shared<ImmuteSetting>(immute),
                 "Making adjust fee setting immutable without authorization",
                 process_result::unauthorized_request);

    char const * token_controller = R"%%%({
        "type": "update_controller",
        "origin": "lgs_1mkqajo9pedc1x764b5y5yzkykcm3h3hx1bumznzhgjqimjpajy9w5qfsis6",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000000000000000000000",
        "sequence": "2",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "CE4F3A844DF04A49B78E1BCF47F4E6300D83253DFC17C084ED9C0B598F19D979",
        "action": "add",
        "controller": {
            "account": "lgs_15p6h3z7dgif1kt8skmdmo8xmobh3xyfzthoden6jqu34t6i4sgtcr4pfj5h",
            "privileges": ["change_issuance", "whitelist", "change_revoke", "issuance", "distribute", "change_adjust_fee", "burn"]
        }
     })%%%";

    tree = get_tree(token_controller);
    UpdateController controller(error, tree);
    assert(!error);
    controller.previous = change_setting.GetHash();
    controller.Sign(controller_2_key.data);

    send_request(std::make_shared<UpdateController>(controller),
                 "Giving controller insufficient authorization",
                 process_result::progress);

    std::cout << "Waiting for controller privilege to change..." << std::endl;
    while(true)
    {
        store->token_account_get(issuance.token_id, account);

        ControllerInfo c;
        auto found = account.GetController(controller_1, c);
        assert(found);

        if(!c.IsAuthorized(TokenSetting::AdjustFee))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - updated value: "
                      << std::boolalpha << account.Allowed(TokenSetting::ModifyAdjustFee)
                      << std::endl;
            break;
        }
    }

    immute.previous = controller.GetHash();
    immute.sequence++;
    immute.Sign(controller_1_key.data);

    send_request(std::make_shared<ImmuteSetting>(immute),
                 "Making adjust fee setting immutable without authorization",
                 process_result::unauthorized_request);

    controller.previous = controller.GetHash();
    controller.controller.privileges.Set(size_t(ControllerPrivilege::ChangeModifyAdjustFee), true);
    controller.sequence++;
    controller.Sign(controller_2_key.data);

    send_request(std::make_shared<UpdateController>(controller),
                 "Giving controller sufficient authorization",
                 process_result::progress);

    std::cout << "Waiting for controller privilege to change..." << std::endl;
    while(true)
    {
        store->token_account_get(issuance.token_id, account);

        ControllerInfo c;
        auto found = account.GetController(controller_1, c);
        assert(found);

        if(!c.IsAuthorized(TokenSetting::ModifyAdjustFee))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - updated value: "
                      << std::boolalpha << account.Allowed(TokenSetting::ModifyAdjustFee)
                      << std::endl;
            break;
        }
    }

    immute.previous = controller.GetHash();
    immute.sequence++;
    immute.Sign(controller_1_key.data);

    send_request(std::make_shared<ImmuteSetting>(immute),
                 "Making adjust fee setting immutable",
                 process_result::progress);

    std::cout << "Waiting for token setting to change..." << std::endl;
    while(true)
    {
        store->token_account_get(issuance.token_id, account);
        if(account.Allowed(TokenSetting::ModifyAdjustFee))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - updated value: "
                      << std::boolalpha << account.Allowed(TokenSetting::ModifyAdjustFee)
                      << std::endl;
            break;
        }
    }

    change_setting.previous = immute.GetHash();
    change_setting.sequence = immute.sequence + 1;
    change_setting.value = SettingValue::Disabled;
    change_setting.Sign(controller_2_key.data);

    send_request(std::make_shared<ChangeSetting>(change_setting),
                 "Modifying immutable setting",
                 process_result::prohibitted_request);

    char const * token_freeze = R"%%%({
        "type": "adjust_user_status",
        "origin": "lgs_15p6h3z7dgif1kt8skmdmo8xmobh3xyfzthoden6jqu34t6i4sgtcr4pfj5h",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000000000000000000000",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "CE4F3A844DF04A49B78E1BCF47F4E6300D83253DFC17C084ED9C0B598F19D979",
        "account": "lgs_1gwfynd84gan8i4rpzzxkikbz7158wha96qpni38rj31hd3dcbrwscey8ahy",
        "status": "frozen"
     })%%%";

    tree = get_tree(token_freeze);
    AdjustUserStatus freeze(error, tree);
    assert(!error);

    freeze.sequence = change_setting.sequence;
    freeze.previous = change_setting.previous;
    freeze.Sign(controller_1_key.data);

    send_request(std::make_shared<AdjustUserStatus>(freeze),
                 "Freezing without sufficient privileges",
                 process_result::unauthorized_request);

    controller.controller.privileges.Set(size_t(ControllerPrivilege::Freeze));
    controller.sequence = freeze.sequence;
    controller.previous = freeze.previous;
    controller.Sign(controller_2_key.data);

    send_request(std::make_shared<UpdateController>(controller),
                 "Giving controller sufficient authorization for freezing",
                 process_result::progress);

    std::cout << "Waiting for controller privilege to change..." << std::endl;
    while(true)
    {
        store->token_account_get(issuance.token_id, account);

        ControllerInfo c;
        auto found = account.GetController(controller_1, c);
        assert(found);

        auto ptr = std::make_shared<AdjustUserStatus>(freeze);
        if(!c.IsAuthorized(ptr))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - updated value: "
                      << std::boolalpha << c.IsAuthorized(ptr)
                      << std::endl;
            break;
        }
    }

    freeze.sequence = controller.sequence + 1;
    freeze.previous = controller.GetHash();
    freeze.Sign(controller_1_key.data);

    send_request(std::make_shared<AdjustUserStatus>(freeze),
                 "Freezing account with sufficient privileges but freezing disabled",
                 process_result::prohibitted_request);

    change_setting.sequence = freeze.sequence;
    change_setting.previous = freeze.previous;
    change_setting.setting = TokenSetting::Freeze;
    change_setting.value = SettingValue::Enabled;
    change_setting.Sign(controller_2_key.data);

    send_request(std::make_shared<ChangeSetting>(change_setting),
                 "Modifying freeze setting",
                 process_result::progress);

    std::cout << "Waiting for setting to change..." << std::endl;
    while(true)
    {
        store->token_account_get(issuance.token_id, account);
        if(!account.Allowed(TokenSetting::Freeze))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - updated value: "
                      << std::boolalpha << account.Allowed(TokenSetting::Freeze)
                      << std::endl;
            break;
        }
    }

    freeze.sequence = change_setting.sequence + 1;
    freeze.previous = change_setting.GetHash();
    freeze.Sign(controller_1_key.data);

    send_request(std::make_shared<AdjustUserStatus>(freeze),
                 "Freezing untethered account with sufficient privileges",
                 process_result::progress);

    std::cout << "Waiting for user status to change..." << std::endl;
    while(true)
    {
        TokenUserStatus status;

        if(store->token_user_status_get(GetTokenUserID(issuance.token_id,
                                                      freeze.account),
                                       status))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - User frozen?: "
                      << std::boolalpha << status.frozen
                      << " user whitelisted?: "
                      << std::boolalpha << status.whitelisted
                      << std::endl;
            break;
        }
    }

    controller.previous = freeze.GetHash();
    controller.sequence = freeze.sequence + 1;
    controller.controller.privileges.Set(size_t(ControllerPrivilege::ChangeWhitelist), true);
    controller.Sign(controller_2_key.data);

    send_request(std::make_shared<UpdateController>(controller),
                 "Giving controller sufficient authorization for modifying whitelisting",
                 process_result::progress);

    std::cout << "Waiting for controller privilege to change..." << std::endl;
    while(true)
    {
        store->token_account_get(issuance.token_id, account);

        ControllerInfo c;
        auto found = account.GetController(controller_1, c);
        assert(found);

        if(!c.IsAuthorized(TokenSetting::Whitelist))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - whitelist setting authorization update value: "
                      << std::boolalpha << c.IsAuthorized(TokenSetting::Whitelist)
                      << std::endl;
            break;
        }
    }

    change_setting.origin = controller_1;
    change_setting.sequence = controller.sequence + 1;
    change_setting.previous = controller.GetHash();
    change_setting.setting = TokenSetting::Whitelist;
    change_setting.value = SettingValue::Disabled;
    change_setting.Sign(controller_1_key.data);

    send_request(std::make_shared<ChangeSetting>(change_setting),
                 "Disabling whitelisting",
                 process_result::progress);

    std::cout << "Waiting for token setting to change..." << std::endl;
    while(true)
    {
        store->token_account_get(issuance.token_id, account);
        if(account.Allowed(TokenSetting::Whitelist))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - whitelist setting updated value: "
                      << std::boolalpha << account.Allowed(TokenSetting::Whitelist)
                      << std::endl;
            break;
        }
    }

    char const * token_account_send = R"%%%({
        "type": "distribute",
        "origin": "lgs_15p6h3z7dgif1kt8skmdmo8xmobh3xyfzthoden6jqu34t6i4sgtcr4pfj5h",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000000000000000000000",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "CE4F3A844DF04A49B78E1BCF47F4E6300D83253DFC17C084ED9C0B598F19D979",
        "transaction" : {
            "destination": "lgs_1gwfynd84gan8i4rpzzxkikbz7158wha96qpni38rj31hd3dcbrwscey8ahy",
            "amount": "5000000"
        }
     })%%%";

    tree = get_tree(token_account_send);
    Distribute distribute(error, tree);

    distribute.sequence = change_setting.sequence + 1;
    distribute.previous = change_setting.GetHash();
    distribute.Sign(controller_1_key.data);

    send_request(std::make_shared<Distribute>(distribute),
                 "Send to frozen account",
                 process_result::frozen);

    distribute.transaction.destination.decode_account("lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    distribute.Sign(controller_1_key.data);

    send_request(std::make_shared<Distribute>(distribute),
                 "Send to unfrozen account",
                 process_result::progress);

    std::cout << "Waiting for user balance to update..." << std::endl;
    while(true)
    {
        account_info info;
        auto result = store->account_get(distribute.transaction.destination, info);
        assert(!result);

        TokenEntry entry;
        if(!info.GetEntry(distribute.token_id, entry))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - token entry added - balance: "
                      << entry.balance.to_string_dec()
                      << std::endl;
            break;
        }
    }

    char const * token_send = R"%%%({
        "type": "token_send",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000000000000000000000",
        "sequence": "0",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "CE4F3A844DF04A49B78E1BCF47F4E6300D83253DFC17C084ED9C0B598F19D979",
        "transactions": [
            {
                 "destination": "lgs_1gwfynd84gan8i4rpzzxkikbz7158wha96qpni38rj31hd3dcbrwscey8ahy",
                 "amount": "200000"
            }
        ],
        "token_fee": "10000"
     })%%%";

    tree = get_tree(token_send);
    TokenSend tokensend(error, tree);
    assert(!error);

    tokensend.Sign(user_2_key.data);

    send_request(std::make_shared<TokenSend>(tokensend),
                 "User send to frozen account",
                 process_result::frozen);

    freeze.sequence = distribute.sequence + 1;
    freeze.previous = distribute.GetHash();
    freeze.status = UserStatus::Unfrozen;
    freeze.Sign(controller_1_key.data);

    send_request(std::make_shared<AdjustUserStatus>(freeze),
                 "Unfreezing user account",
                 process_result::progress);

    std::cout << "Waiting for user status to change..." << std::endl;
    while(true)
    {
        TokenUserStatus status;
        store->token_user_status_get(GetTokenUserID(issuance.token_id,
                                                   freeze.account),
                                    status);
        if(status.frozen)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - User frozen?: "
                      << std::boolalpha << status.frozen
                      << " user whitelisted?: "
                      << std::boolalpha << status.whitelisted
                      << std::endl;
            break;
        }
    }

    send_request(std::make_shared<TokenSend>(tokensend),
                 "User send with insufficient token fee",
                 process_result::insufficient_token_fee);

    tokensend.token_fee = 50000;
    tokensend.Sign(user_2_key.data);

    send_request(std::make_shared<TokenSend>(tokensend),
                 "User send",
                 process_result::progress);

    std::cout << "Waiting for balances to update..." << std::endl;
    while(true)
    {
        account_info info;
        account_info sender;
        auto result = store->account_get(tokensend.transactions[0].destination, info);
        assert(!result);
        result = store->account_get(tokensend.origin, sender);
        assert(!result);

        store->token_account_get(tokensend.token_id, account);

        TokenEntry entry;
        TokenEntry sender_entry;
        sender.GetEntry(tokensend.token_id, sender_entry);

        if(!info.GetEntry(tokensend.token_id, entry) ||
           account.token_fee_balance == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - token entry added - sender balance: "
                      << sender_entry.balance.to_string_dec()
                      << " destination balance: "
                      << entry.balance.to_string_dec()
                      << " token account fee balance: "
                      << account.token_fee_balance.to_string_dec()
                      << std::endl;
            break;
        }
    }

    char const * token_burn = R"%%%({
        "type": "burn",
        "origin": "lgs_15p6h3z7dgif1kt8skmdmo8xmobh3xyfzthoden6jqu34t6i4sgtcr4pfj5h",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000000000000000000000",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "CE4F3A844DF04A49B78E1BCF47F4E6300D83253DFC17C084ED9C0B598F19D979",
        "amount": "20000"
     })%%%";

    tree = get_tree(token_burn);
    Burn burn(error, tree);
    assert(!error);

    burn.sequence = freeze.sequence + 1;
    burn.previous = freeze.GetHash();
    burn.Sign(controller_1_key.data);

    send_request(std::make_shared<Burn>(burn),
                 "Burning 20000 tokens",
                 process_result::progress);

    std::cout << "Waiting for token balance to update..." << std::endl;
    while(true)
    {
        auto result = store->token_account_get(burn.token_id, account);
        assert(!result);

        if(account.token_balance != issuance.total_supply
                                    - burn.amount
                                    - distribute.transaction.amount
                                    + issue_adtl.amount)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - token account balance: "
                      << account.token_balance.to_string_dec()
                      << std::endl;
            break;
        }
    }

    char const * token_account_withdraw_fee = R"%%%({
        "type": "withdraw_fee",
        "origin": "lgs_1mkqajo9pedc1x764b5y5yzkykcm3h3hx1bumznzhgjqimjpajy9w5qfsis6",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000000000000000000000",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "CE4F3A844DF04A49B78E1BCF47F4E6300D83253DFC17C084ED9C0B598F19D979",
        "transaction" : {
            "destination": "lgs_1mkqajo9pedc1x764b5y5yzkykcm3h3hx1bumznzhgjqimjpajy9w5qfsis6",
            "amount": "500000"
        }
     })%%%";

    tree = get_tree(token_account_withdraw_fee);
    WithdrawFee withdraw_fee(error, tree);
    assert(!error);

    withdraw_fee.sequence = burn.sequence + 1;
    withdraw_fee.previous = burn.GetHash();
    withdraw_fee.Sign(controller_2_key.data);

    send_request(std::make_shared<WithdrawFee>(withdraw_fee),
                 "Withdrawing too much",
                 process_result::insufficient_token_balance);

    withdraw_fee.transaction.amount = 50000;
    withdraw_fee.Sign(controller_2_key.data);

    send_request(std::make_shared<WithdrawFee>(withdraw_fee),
                 "Withdrawing fee",
                 process_result::progress);

    std::cout << "Waiting for user balance to update..." << std::endl;
    while(true)
    {
        auto result = store->token_account_get(withdraw_fee.token_id, account);
        assert(!result);

        account_info info;
        store->account_get(controller_2, info);

        TokenEntry entry;
        if(!account.token_fee_balance.is_zero() || !info.GetEntry(withdraw_fee.token_id, entry))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - token fee balance: "
                      << account.token_fee_balance.to_string_dec()
                      << " controller account balance: "
                      << entry.balance.to_string_dec()
                      << std::endl;
            break;
        }
    }

    char const * token_issuer_info = R"%%%({
        "type": "update_issuer_info",
        "origin": "lgs_1mkqajo9pedc1x764b5y5yzkykcm3h3hx1bumznzhgjqimjpajy9w5qfsis6",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000000000000000000000",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "CE4F3A844DF04A49B78E1BCF47F4E6300D83253DFC17C084ED9C0B598F19D979",
        "new_info": "This is new info"
     })%%%";

    tree = get_tree(token_issuer_info);
    UpdateIssuerInfo issuer_info(error, tree);
    assert(!error);

    issuer_info.sequence = withdraw_fee.sequence + 1;
    issuer_info.previous = withdraw_fee.GetHash();
    issuer_info.Sign(controller_2_key.data);

    send_request(std::make_shared<UpdateIssuerInfo>(issuer_info),
                 "Updating issuer info",
                 process_result::progress);

    std::cout << "Waiting for issuer info to update..." << std::endl;
    while(true)
    {
        auto result = store->token_account_get(issuer_info.token_id, account);
        assert(!result);

        if(account.issuer_info != issuer_info.new_info)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - issuer info: "
                      << account.issuer_info
                      << std::endl;
            break;
        }
    }

    change_setting.sequence = issuer_info.sequence + 1;
    change_setting.previous = issuer_info.GetHash();
    change_setting.setting = TokenSetting::Whitelist;
    change_setting.value = SettingValue::Enabled;
    change_setting.Sign(controller_1_key.data);

    send_request(std::make_shared<ChangeSetting>(change_setting),
                 "Enabling whitelisting",
                 process_result::progress);

    std::cout << "Waiting for token setting to change..." << std::endl;
    while(true)
    {
        store->token_account_get(issuance.token_id, account);
        if(!account.Allowed(TokenSetting::Whitelist))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - whitelist setting updated value: "
                      << std::boolalpha << account.Allowed(TokenSetting::Whitelist)
                      << std::endl;
            break;
        }
    }

    char const * adjust_status_json = R"%%%({
        "type": "adjust_user_status",
        "origin": "lgs_15p6h3z7dgif1kt8skmdmo8xmobh3xyfzthoden6jqu34t6i4sgtcr4pfj5h",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000000000000000000000",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "account": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "status": "whitelisted"
     })%%%";

    tree = get_tree(adjust_status_json);
    AdjustUserStatus adjust_status(error, tree);
    assert(!error);

    adjust_status.token_id = GetTokenID(issuance);
    adjust_status.sequence = change_setting.sequence + 1;
    adjust_status.previous = change_setting.GetHash();
    adjust_status.Sign(controller_1_key.data);

    send_request(std::make_shared<AdjustUserStatus>(adjust_status),
                 "Whitelisting user",
                 process_result::progress);

    std::cout << "Waiting for user status to change..." << std::endl;
    while(true)
    {
        logos::account_info info;
        auto result = store->account_get(adjust_status.account, info);
        assert(!result);

        TokenEntry entry;
        result = info.GetEntry(adjust_status.token_id, entry);
        assert(result);

        if(!entry.status.whitelisted)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - User frozen?: "
                      << std::boolalpha << entry.status.frozen
                      << " user whitelisted?: "
                      << std::boolalpha << entry.status.whitelisted
                      << std::endl;
            break;
        }
    }

    tokensend.previous = tokensend.GetHash();
    tokensend.sequence++;
    tokensend.Sign(user_2_key.data);

    send_request(std::make_shared<TokenSend>(tokensend),
                 "User send to unwhitelisted account",
                 process_result::not_whitelisted);

    adjust_status.sequence++;
    adjust_status.previous = adjust_status.GetHash();
    adjust_status.account.decode_account("lgs_1gwfynd84gan8i4rpzzxkikbz7158wha96qpni38rj31hd3dcbrwscey8ahy");
    adjust_status.Sign(controller_1_key.data);

    send_request(std::make_shared<AdjustUserStatus>(adjust_status),
                 "Whitelisting recipient",
                 process_result::progress);

    std::cout << "Waiting for user status to change..." << std::endl;
    while(true)
    {
        logos::account_info info;
        auto result = store->account_get(adjust_status.account, info);
        assert(!result);

        TokenEntry entry;
        result = info.GetEntry(adjust_status.token_id, entry);
        assert(result);

        if(!entry.status.whitelisted)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - User frozen?: "
                      << std::boolalpha << entry.status.frozen
                      << " user whitelisted?: "
                      << std::boolalpha << entry.status.whitelisted
                      << std::endl;
            break;
        }
    }

    send_request(std::make_shared<TokenSend>(tokensend),
                 "User send to whitelisted account",
                 process_result::progress);

    std::cout << "Waiting for balances to update..." << std::endl;
    while(true)
    {
        account_info info;
        account_info sender;
        auto result = store->account_get(tokensend.transactions[0].destination, info);
        assert(!result);
        result = store->account_get(tokensend.origin, sender);
        assert(!result);

        store->token_account_get(tokensend.token_id, account);

        TokenEntry entry;
        TokenEntry sender_entry;
        sender.GetEntry(tokensend.token_id, sender_entry);

        bool did = false;
        if(!info.GetEntry(tokensend.token_id, entry) ||
           account.token_fee_balance == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - sender balance: "
                      << sender_entry.balance.to_string_dec()
                      << " destination balance: "
                      << entry.balance.to_string_dec()
                      << " token account fee balance: "
                      << account.token_fee_balance.to_string_dec()
                      << std::endl;
            break;
        }
    }

    tokensend.previous = tokensend.GetHash();
    tokensend.sequence++;
    tokensend.Sign(user_2_key.data);

    send_request(std::make_shared<TokenSend>(tokensend),
                 "User send to again",
                 process_result::progress);

    std::cout << "Waiting for balances to update..." << std::endl;
    while(true)
    {
        account_info info;
        account_info sender;
        auto result = store->account_get(tokensend.transactions[0].destination, info);
        assert(!result);
        result = store->account_get(tokensend.origin, sender);
        assert(!result);

        store->token_account_get(tokensend.token_id, account);

        TokenEntry entry;
        info.GetEntry(tokensend.token_id, entry);

        TokenEntry sender_entry;
        sender.GetEntry(tokensend.token_id, sender_entry);

        bool did = false;
        if(account.token_fee_balance == 50000)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - sender balance: "
                      << sender_entry.balance.to_string_dec()
                      << " destination balance: "
                      << entry.balance.to_string_dec()
                      << " token account fee balance: "
                      << account.token_fee_balance.to_string_dec()
                      << std::endl;
            break;
        }
    }

    issue_adtl.amount = ~(uint128_t)(0);
    issue_adtl.amount -= 6000000000000 - 1;
    std::cout << issue_adtl.amount.to_string_dec() << std::endl;
    issue_adtl.sequence = adjust_status.sequence + 1;
    issue_adtl.previous = adjust_status.GetHash();
    issue_adtl.Sign(controller_1_key.data);

    send_request(std::make_shared<IssueAdditional>(issue_adtl),
                 "Issueing too many additional tokens",
                 process_result::total_supply_overflow);

    burn.amount = 7000000000000;
    burn.sequence = issue_adtl.sequence;
    burn.previous = issue_adtl.previous;
    burn.Sign(controller_1_key.data);

    send_request(std::make_shared<Burn>(burn),
                 "Burning too many tokens",
                 process_result::insufficient_token_balance);

    burn.amount = account.token_balance;
    burn.Sign(controller_1_key.data);

    send_request(std::make_shared<Burn>(burn),
                 "Burning all central tokens",
                 process_result::progress);

    std::cout << "Waiting for user balance to update..." << std::endl;
    while(true)
    {
        auto result = store->token_account_get(burn.token_id, account);
        assert(!result);

        if(account.token_balance != 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - token account balance: "
                      << account.token_balance.to_string_dec()
                      << std::endl;
            break;
        }
    }

    issue_adtl.amount = ~(uint128_t)(0);
    issue_adtl.amount -= 5000000;
    issue_adtl.sequence = burn.sequence + 1;
    issue_adtl.previous = burn.GetHash();
    issue_adtl.Sign(controller_1_key.data);

    send_request(std::make_shared<IssueAdditional>(issue_adtl),
                 "Issueing max additional tokens",
                 process_result::progress);

    std::cout << "Waiting for user balance to update..." << std::endl;
    while(true)
    {
        auto result = store->token_account_get(burn.token_id, account);
        assert(!result);

        if(account.token_balance == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - token account balance: "
                      << account.token_balance.to_string_dec()
                      << std::endl;
            break;
        }
    }

    send.origin = issuer;
    send.transactions.clear();
    send.transactions.push_back(TokenRequest::Transaction(issuance.token_id, {999}));
    send.sequence = 1;
    send.previous = issuance.GetHash();
    send.Sign(issuer_key.data);

    error = store->token_account_get(burn.token_id, account);
    assert(!error);

    std::cout << "Current token account logos balance: "
              << account.GetBalance().to_string_dec()
              << std::endl;

    auto talb = account.GetBalance();

    send_request(std::make_shared<Send>(send),
                 "Sending logos to token",
                 process_result::progress);

    std::cout << "Waiting for token account logos balance to update..." << std::endl;
    while(true)
    {
        auto result = store->token_account_get(burn.token_id, account);
        assert(!result);

        if(account.GetBalance() == talb)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - token account logos balance: "
                      << account.GetBalance().to_string_dec()
                      << std::endl;
            break;
        }
    }

    char const * withdraw_logos_json = R"%%%({
        "type": "withdraw_logos",
        "origin": "lgs_1mkqajo9pedc1x764b5y5yzkykcm3h3hx1bumznzhgjqimjpajy9w5qfsis6",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000000000000000000000",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "CE4F3A844DF04A49B78E1BCF47F4E6300D83253DFC17C084ED9C0B598F19D979",
        "transaction" : {
            "destination": "lgs_1mkqajo9pedc1x764b5y5yzkykcm3h3hx1bumznzhgjqimjpajy9w5qfsis6",
            "amount": "10000000000000000000050"
        }
     })%%%";

    tree = get_tree(withdraw_logos_json);
    WithdrawLogos wl(error, tree);
    assert(!error);

    wl.sequence = issue_adtl.sequence + 1;
    wl.previous = issue_adtl.GetHash();
    wl.Sign(controller_2_key.data);

    send_request(std::make_shared<WithdrawLogos>(wl),
                 "Withdrawing Logos",
                 process_result::progress);

    std::cout << "Waiting for controller's balance to update..." << std::endl;
    while(true)
    {
        account_info info;
        store->account_get(controller_2, info);

        amount balance;
        balance.decode_dec("10000000000000000000000000");

        if(info.GetBalance() == balance)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - balance: "
                      << info.GetBalance().to_string_dec()
                      << std::endl;
            break;
        }
    }

    freeze.sequence = wl.sequence + 1;
    freeze.origin = controller_2;
    freeze.previous = wl.GetHash();
    freeze.account = controller_2;
    freeze.status = UserStatus::Whitelisted;
    freeze.Sign(controller_2_key.data);

    send_request(std::make_shared<AdjustUserStatus>(freeze),
                 "Whitelisting Controller",
                 process_result::progress);

    std::cout << "Waiting for user status to change..." << std::endl;
    while(true)
    {
        auto result = store->token_account_get(withdraw_fee.token_id, account);
        assert(!result);

        account_info info;
        store->account_get(controller_2, info);

        TokenEntry entry;
        info.GetEntry(withdraw_fee.token_id, entry);

        if(!entry.status.whitelisted)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - User frozen?: "
                      << std::boolalpha << entry.status.frozen
                      << " user whitelisted?: "
                      << std::boolalpha << entry.status.whitelisted
                      << std::endl;
            break;
        }
    }

    char const * revoke_json = R"%%%({
        "type": "revoke",
        "origin": "lgs_1mkqajo9pedc1x764b5y5yzkykcm3h3hx1bumznzhgjqimjpajy9w5qfsis6",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000000000000000000000",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "CE4F3A844DF04A49B78E1BCF47F4E6300D83253DFC17C084ED9C0B598F19D979",
        "source": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "transaction" : {
            "destination": "lgs_1mkqajo9pedc1x764b5y5yzkykcm3h3hx1bumznzhgjqimjpajy9w5qfsis6",
            "amount": "100000"
        }
     })%%%";

    tree = get_tree(revoke_json);
    Revoke revoke(error, tree);
    assert(!error);

    revoke.sequence = freeze.sequence + 1;
    revoke.previous = freeze.GetHash();
    revoke.Sign(controller_2_key.data);

    send_request(std::make_shared<Revoke>(revoke),
                 "Revoking tokens",
                 process_result::progress);

    std::cout << "Waiting for controller's balance to update..." << std::endl;
    while(true)
    {
        account_info info;
        store->account_get(controller_2, info);

        TokenEntry entry;
        info.GetEntry(revoke.token_id, entry);

        if(entry.balance == 50000)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            std::cout << "Done - Controller balance: "
                      << entry.balance.to_string_dec()
                      << std::endl;
            break;
        }
    }

    revoke.sequence++;
    revoke.transaction.amount = 10;
    revoke.previous = revoke.GetHash();
    revoke.Sign(controller_2_key.data);

    logos::process_return result;
    persistence.ValidateAndUpdate(std::make_shared<Revoke>(revoke),
                                  0,
                                  result,
                                  false);

    ASSERT_EQ(ProcessResultToString(process_result::progress),
              ProcessResultToString(result.code));

    tokensend.previous = tokensend.GetHash();
    tokensend.sequence++;
    tokensend.Sign(user_2_key.data);

    persistence.ValidateAndUpdate(std::make_shared<TokenSend>(tokensend),
                                  0,
                                  result,
                                  false);

    ASSERT_EQ(ProcessResultToString(process_result::already_reserved),
              ProcessResultToString(result.code));

    apply_request(std::make_shared<Revoke>(revoke));

    persistence.ValidateAndUpdate(std::make_shared<TokenSend>(tokensend),
                                  0,
                                  result,
                                  false);

    ASSERT_EQ(ProcessResultToString(process_result::progress),
              ProcessResultToString(result.code));

    revoke.sequence++;
    revoke.previous = revoke.GetHash();
    revoke.Sign(controller_2_key.data);

    persistence.ValidateAndUpdate(std::make_shared<Revoke>(revoke),
                                  0,
                                  result,
                                  false);

    ASSERT_EQ(ProcessResultToString(process_result::already_reserved),
              ProcessResultToString(result.code));

    apply_request(std::make_shared<TokenSend>(tokensend));
}

#endif // #ifdef Unit_Test_Token_Requests
