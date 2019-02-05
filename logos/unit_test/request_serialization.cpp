#include <gtest/gtest.h>

#include <logos/token/requests.hpp>
#include <logos/request/change.hpp>

#include <logos/epoch/election_requests.hpp>

#include <boost/property_tree/ptree.hpp>

#define Unit_Test_Request_Serialization

#ifdef Unit_Test_Request_Serialization

TEST (Request_Serialization, json_deserialization)
{
    // Convenience method for producing a
    // boost property_tree from a string
    // literal.
    //
    auto get_tree = [](char const * json)
    {
        boost::iostreams::array_source as(json, strlen(json));
        boost::iostreams::stream<boost::iostreams::array_source> is(as);

        boost::property_tree::ptree tree;
        boost::property_tree::read_json(is, tree);

        return tree;
    };

    // Token Issuance
    //
    //
    char const * token_issuance = R"%%%({
        "type": "issue",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "symbol": "MYC",
        "name": "MyCoin",
        "total_supply": "65000",
        "settings": ["add", "modify_add", "whitelist"],
        "controllers": [
            {
                "account": "lgs_19bxabqmra8ijd8s3qs3u611z5wss6amnem4bht6u9e3odpfper7ed1i7yds",
                "privileges": [
                    "change_add",
                    "change_revoke"
                ]
            },
            {
                "account": "lgs_3dwpb16qw5eh6yt5c3waobn9y113pg6epnsbiy8uo3c5q3m5onpbye1u8tw6",
                "privileges": [
                    "change_add",
                    "change_revoke",
                    "change_freeze",
                    "withdraw_fee"
                ]
            }
        ],
        "issuer_info": "MyCoin is a coin owned by me."
     })%%%";

    boost::property_tree::ptree tree = get_tree(token_issuance);
    bool error = false;
    TokenIssuance issuance(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(issuance.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(issuance.fee.number(), 100);
    ASSERT_EQ(issuance.sequence, 1);
    ASSERT_EQ(issuance.symbol, "MYC");
    ASSERT_EQ(issuance.total_supply, 65000);
    ASSERT_EQ(issuance.controllers.size(), 2);
    ASSERT_TRUE(issuance.settings[size_t(TokenSetting::AddTokens)]);
    ASSERT_FALSE(issuance.settings[size_t(TokenSetting::ModifyWhitelist)]);
    ASSERT_TRUE(issuance.controllers[1].privileges[size_t(ControllerPrivilege::ChangeFreeze)]);

    // Token Issue Additional
    //
    //
    char const * token_issue_adtl = R"%%%({
        "type": "issue",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "amount": "50000"
     })%%%";

    tree = get_tree(token_issue_adtl);
    TokenIssueAdtl issue_adtl(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(issue_adtl.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(issue_adtl.fee.number(), 100);
    ASSERT_EQ(issue_adtl.sequence, 1);
    ASSERT_EQ(issue_adtl.amount, 50000);

    // Token Change Setting
    //
    //
    char const * token_change_setting = R"%%%({
        "type": "issue",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "setting": "FREEZE",
        "value": "true"
     })%%%";

    tree = get_tree(token_change_setting);
    TokenChangeSetting change_setting(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(change_setting.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(change_setting.fee.number(), 100);
    ASSERT_EQ(change_setting.sequence, 1);
    ASSERT_EQ(change_setting.setting, TokenSetting::Freeze);
    ASSERT_EQ(change_setting.value, SettingValue::Enabled);

    // Token Immute Setting
    //
    //
    char const * token_immute_setting = R"%%%({
        "type": "issue",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "setting": "modify_freeze"
     })%%%";

    tree = get_tree(token_immute_setting);
    TokenImmuteSetting immute_setting(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(immute_setting.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(immute_setting.fee.number(), 100);
    ASSERT_EQ(immute_setting.sequence, 1);
    ASSERT_EQ(immute_setting.setting, TokenSetting::ModifyFreeze);

    // Token Revoke
    //
    //
    char const * token_revoke = R"%%%({
        "type": "issue",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "source": "lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju",
        "transaction" : {
            "destination": "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz",
            "amount": "50"
        }
     })%%%";

    tree = get_tree(token_revoke);
    TokenRevoke revoke(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(revoke.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(revoke.fee.number(), 100);
    ASSERT_EQ(revoke.sequence, 1);
    ASSERT_EQ(revoke.source.to_account(), "lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    ASSERT_EQ(revoke.transaction.destination.to_account(),
              "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz");
    ASSERT_EQ(revoke.transaction.amount, 50);

    // Token Freeze
    //
    //
    char const * token_freeze = R"%%%({
        "type": "issue",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "account": "lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju",
        "action": "unfreeze"
     })%%%";

    tree = get_tree(token_freeze);
    TokenFreeze freeze(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(freeze.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(freeze.fee.number(), 100);
    ASSERT_EQ(freeze.sequence, 1);
    ASSERT_EQ(freeze.account.to_account(), "lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    ASSERT_EQ(freeze.action, FreezeAction::Unfreeze);

    // Token Set Fee
    //
    //
    char const * token_set_fee = R"%%%({
        "type": "issue",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee_type": "percentage",
        "fee_rate": "10"
     })%%%";

    tree = get_tree(token_set_fee);
    TokenSetFee set_fee(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(set_fee.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(set_fee.fee.number(), 100);
    ASSERT_EQ(set_fee.sequence, 1);
    ASSERT_EQ(set_fee.fee_type, TokenFeeType::Percentage);
    ASSERT_EQ(set_fee.fee_rate, 10);

    // Token Whitelist
    //
    //
    char const * token_whitelist = R"%%%({
        "type": "issue",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "account": "lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju"
     })%%%";

    tree = get_tree(token_whitelist);
    TokenWhitelist whitelist(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(whitelist.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(whitelist.fee.number(), 100);
    ASSERT_EQ(whitelist.sequence, 1);
    ASSERT_EQ(whitelist.account.to_account(), "lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");

    // Token Issuer Info
    //
    //
    char const * token_issuer_info = R"%%%({
        "type": "issue",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "new_info": "This is new info"
     })%%%";

    tree = get_tree(token_issuer_info);
    TokenIssuerInfo issuer_info(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(issuer_info.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(issuer_info.fee.number(), 100);
    ASSERT_EQ(issuer_info.sequence, 1);
    ASSERT_EQ(issuer_info.new_info, "This is new info");

    // Token Controller
    //
    //
    char const * token_controller = R"%%%({
        "type": "issue",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "action": "add",
        "controller": {
            "account": "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz",
            "privileges": ["change_add", "withdraw_fee"]
        }
     })%%%";

    tree = get_tree(token_controller);
    TokenController controller(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(controller.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(controller.fee.number(), 100);
    ASSERT_EQ(controller.sequence, 1);
    ASSERT_EQ(controller.action, ControllerAction::Add);
    ASSERT_EQ(controller.controller.account.to_account(), "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz");
    ASSERT_TRUE(controller.controller.privileges[size_t(ControllerPrivilege::ChangeAddTokens)]);
    ASSERT_TRUE(controller.controller.privileges[size_t(ControllerPrivilege::WithdrawFee)]);
    ASSERT_FALSE(controller.controller.privileges[size_t(ControllerPrivilege::PromoteController)]);
    ASSERT_FALSE(controller.controller.privileges[size_t(ControllerPrivilege::Revoke)]);
    ASSERT_FALSE(controller.controller.privileges[size_t(ControllerPrivilege::AdjustFee)]);

    // Token Burn
    //
    //
    char const * token_burn = R"%%%({
        "type": "issue",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "amount": "1000"
     })%%%";

    tree = get_tree(token_burn);
    TokenBurn burn(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(burn.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(burn.fee.number(), 100);
    ASSERT_EQ(burn.sequence, 1);
    ASSERT_EQ(burn.amount, 1000);

    // Token Account Send
    //
    //
    char const * token_account_send = R"%%%({
        "type": "issue",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "transaction" : {
            "destination": "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz",
            "amount": "100"
        }
     })%%%";

    tree = get_tree(token_account_send);
    TokenAccountSend account_send(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(account_send.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(account_send.fee.number(), 100);
    ASSERT_EQ(account_send.sequence, 1);
    ASSERT_EQ(account_send.transaction.destination.to_account(),
              "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz");
    ASSERT_EQ(account_send.transaction.amount, 100);

    // Token Account Withdraw Fee
    //
    //
    char const * token_account_withdraw_fee = R"%%%({
        "type": "issue",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "transaction" : {
            "destination": "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz",
            "amount": "50"
        }
     })%%%";

    tree = get_tree(token_account_withdraw_fee);
    TokenAccountWithdrawFee withdraw_fee(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(withdraw_fee.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(withdraw_fee.fee.number(), 100);
    ASSERT_EQ(withdraw_fee.sequence, 1);
    ASSERT_EQ(withdraw_fee.transaction.destination.to_account(),
              "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz");
    ASSERT_EQ(withdraw_fee.transaction.amount, 50);

    // Token Send
    //
    //
    char const * token_send = R"%%%({
        "type": "issue",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "transactions": [
            {
                 "destination": "lgs_1sibjaeaceh59dh7fefo49narpsoytqac5hafhujum3grnd7qrhbczfy9wx8",
                 "amount": "1"
            },
            {
                 "destination": "lgs_15p6h3z7dgif1kt8skmdmo8xmobh3xyfzthoden6jqu34t6i4sgtcr4pfj5h",
                 "amount": "2"
            },
            {
                 "destination": "lgs_1mkqajo9pedc1x764b5y5yzkykcm3h3hx1bumznzhgjqimjpajy9w5qfsis6",
                 "amount": "3"
            }
        ],
        "token_fee": "5"
     })%%%";

    tree = get_tree(token_send);
    TokenSend send(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(send.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(send.fee.number(), 100);
    ASSERT_EQ(send.sequence, 1);
    ASSERT_EQ(send.transactions.size(), 3);
    ASSERT_EQ(send.transactions[0].destination.to_account(),
              "lgs_1sibjaeaceh59dh7fefo49narpsoytqac5hafhujum3grnd7qrhbczfy9wx8");
    ASSERT_EQ(send.transactions[0].amount, 1);
    ASSERT_EQ(send.transactions[1].destination.to_account(),
              "lgs_15p6h3z7dgif1kt8skmdmo8xmobh3xyfzthoden6jqu34t6i4sgtcr4pfj5h");
    ASSERT_EQ(send.transactions[1].amount, 2);
    ASSERT_EQ(send.transactions[2].destination.to_account(),
              "lgs_1mkqajo9pedc1x764b5y5yzkykcm3h3hx1bumznzhgjqimjpajy9w5qfsis6");
    ASSERT_EQ(send.transactions[2].amount, 3);
    ASSERT_EQ(send.token_fee, 5);

    // Change Representative
    //
    //
    char const * native_change = R"%%%({
        "type": "change",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "client": "lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju",
        "representative": "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz"
     })%%%";

    tree = get_tree(native_change);
    Change change(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(change.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(change.fee.number(), 100);
    ASSERT_EQ(change.sequence, 1);
    ASSERT_EQ(change.client.to_account(),
              "lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    ASSERT_EQ(change.representative.to_account(),
              "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz");
}


TEST (Request_Serialization, election_requests_json)
{
    auto get_tree = [](char const * json)
    {
        boost::iostreams::array_source as(json, strlen(json));
        boost::iostreams::stream<boost::iostreams::array_source> is(as);

        boost::property_tree::ptree tree;
        boost::property_tree::read_json(is, tree);

        return tree;
    };
    
    char const * announce_candidacy_json = R"%%%({
        "type": "announce_candidacy",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000"

     })%%%";

    boost::property_tree::ptree tree(get_tree(announce_candidacy_json));
    bool error = false;

    AnnounceCandidacy announce_candidacy(error, tree);
    ASSERT_FALSE(error);
    ASSERT_EQ(announce_candidacy.type,RequestType::AnnounceCandidacy);
    //try to make the wrong type of request
    RenounceCandidacy renounce_candidacy(error, tree);
    ASSERT_TRUE(error);

    char const * renounce_candidacy_json = R"%%%({
        "type": "renounce_candidacy",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000"

     })%%%";

    tree = get_tree(renounce_candidacy_json);
    error = false;

    renounce_candidacy = RenounceCandidacy(error, tree);
    ASSERT_FALSE(error);
    ASSERT_EQ(renounce_candidacy.type,RequestType::RenounceCandidacy);
    //try to make the wrong type of request
    announce_candidacy = AnnounceCandidacy(error, tree);
    ASSERT_TRUE(error);

    char const * election_vote_empty_json = R"%%%({
        "type": "election_vote",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",

        "request":
        {
            "votes":
             {}
        }
     })%%%";

    error = false;
    tree = get_tree(election_vote_empty_json);
    ElectionVote ev(error,tree);
    ASSERT_FALSE(error);
    ASSERT_EQ(ev.votes_.size(),0);
    ASSERT_EQ(ev.type,RequestType::ElectionVote);


    char const * election_vote_single_json = R"%%%({
        "type": "election_vote",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",

        "request":
        {
            "votes":
             {
                "lgs_39y3y5d1wd5hrgqohoxoi97t6rjeayp3kjb7du71wq3j6s8k7x66gcfo9dy1" : "8"
             }
        }
     })%%%";

    error = false;
    tree = get_tree(election_vote_single_json);
    ev = ElectionVote(error,tree);
    ASSERT_FALSE(error);
    ASSERT_EQ(ev.votes_.size(),1);
    ASSERT_EQ(ev.type,RequestType::ElectionVote);
    ASSERT_EQ(ev.votes_[0].account.to_account(),"lgs_39y3y5d1wd5hrgqohoxoi97t6rjeayp3kjb7du71wq3j6s8k7x66gcfo9dy1");
    ASSERT_EQ(ev.votes_[0].num_votes,8);


    char const * election_vote_multiple_votes_json = R"%%%({
        "type": "election_vote",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",

        "request":
        {
            "votes":
             {
                "lgs_39y3y5d1wd5hrgqohoxoi97t6rjeayp3kjb7du71wq3j6s8k7x66gcfo9dy1" : "1",
                "lgs_1urumj6bwwrww3ozuf7u7mp4knebsjijnzimoiadnmde87sxqg5wtx5zjh4k" : "2",
                "lgs_1qmunbhffeubzygbmf1w9hmb65jphjcig5xm6j754uhpoy1gfnt9or1s8y6r" : "3",
                "lgs_3u6hrty88rsne614rx57mck9y84y5chcna77md6p7gkm5eqqm1awwya4nctm" : "4"
             }
        }
     })%%%";

    error = false;
    tree = get_tree(election_vote_multiple_votes_json);
    ev = ElectionVote(error,tree);
    ASSERT_FALSE(error);
    ASSERT_EQ(ev.votes_.size(),4);
    ASSERT_EQ(ev.type,RequestType::ElectionVote);
    uint8_t total_votes = 0;
    for(auto const & v: ev.votes_)
    {
        if(v.account.to_account() == "lgs_39y3y5d1wd5hrgqohoxoi97t6rjeayp3kjb7du71wq3j6s8k7x66gcfo9dy1")
        {
            ASSERT_EQ(v.num_votes,1);
            total_votes += v.num_votes;
        }
        if(v.account.to_account() == "lgs_1urumj6bwwrww3ozuf7u7mp4knebsjijnzimoiadnmde87sxqg5wtx5zjh4k")
        {
            ASSERT_EQ(v.num_votes,2);
            total_votes += v.num_votes;
        }
        if(v.account.to_account() == "lgs_1qmunbhffeubzygbmf1w9hmb65jphjcig5xm6j754uhpoy1gfnt9or1s8y6r")
        {
            ASSERT_EQ(v.num_votes,3);
            total_votes += v.num_votes;
        }
        if(v.account.to_account() == "lgs_3u6hrty88rsne614rx57mck9y84y5chcna77md6p7gkm5eqqm1awwya4nctm")
        {
            ASSERT_EQ(v.num_votes,4);
            total_votes += v.num_votes;
        }
    } 
    ASSERT_EQ(total_votes,10);

    //back and forth
    std::string json_string = ev.ToJson();
    tree = get_tree(json_string.c_str());
    error = false;
    ElectionVote ev2(error, tree);
    ASSERT_FALSE(error);
    ASSERT_EQ(ev.votes_,ev2.votes_);


    
    //consistency with streams
    std::vector<uint8_t> buf;
    {
        logos::vectorstream write_stream(buf);
        uint64_t size = ev.Serialize(write_stream);
        ASSERT_EQ(size,ev.WireSize());
    }

    logos::bufferstream read_stream(buf.data(), buf.size());
    error = false;
    ElectionVote ev3(error,read_stream);
    ASSERT_FALSE(error);
    ASSERT_EQ(ev,ev3);



}



TEST (Request_Serialization, election_requests_stream)
{


    //CandidateVotePair
    ElectionVote::CandidateVotePair p(123,1);
    {
        std::vector<uint8_t> buf;
        {
            logos::vectorstream write_stream(buf);
            uint64_t size = p.Serialize(write_stream);
            ASSERT_EQ(size,p.WireSize());
        }

        logos::bufferstream read_stream(buf.data(), buf.size());
        bool error = false;
        ElectionVote::CandidateVotePair p2(error,read_stream);
        ASSERT_FALSE(error);
        ASSERT_EQ(p,p2);
    }

    //no votes
    BlockHash prev1 = 123;
    AccountSig sig1 = 1;
    AccountAddress address = 1;
    Amount fee = 23;
    uint32_t sequence = 7;
    ElectionVote ev(address,prev1,fee,sequence,sig1);
    {
        std::vector<uint8_t> buf;
        {
            logos::vectorstream write_stream(buf);
            uint64_t size = ev.Serialize(write_stream);
            ASSERT_EQ(size,ev.WireSize());
        }

        logos::bufferstream read_stream(buf.data(), buf.size());
        bool error = false;
        ElectionVote ev2(error,read_stream);
        ASSERT_FALSE(error);
        ASSERT_EQ(ev,ev2);
    }


    //4 votes
    ElectionVote::CandidateVotePair p1(123,1);
    ElectionVote::CandidateVotePair p2(456,2);
    ElectionVote::CandidateVotePair p3(789,3);
    ElectionVote::CandidateVotePair p4(101112,4);
    std::vector<ElectionVote::CandidateVotePair> votes = {p1,p2,p3,p4};

    ev.votes_ = votes;
    {
        std::vector<uint8_t> buf;
        {
            logos::vectorstream write_stream(buf);
            uint64_t size = ev.Serialize(write_stream);
            ASSERT_EQ(size,ev.WireSize());
        }

        logos::bufferstream read_stream(buf.data(), buf.size());
        bool error = false;
        ElectionVote ev2(error,read_stream);
        ASSERT_FALSE(error);
        ASSERT_EQ(ev,ev2);
    }


    //manual stream
    {
        BlockHash prev = ev.previous;
        BlockHash next = ev.next;
        AccountSig sig = ev.signature;
        AccountAddress origin = ev.origin;
        RequestType type = RequestType::ElectionVote;
        std::vector<uint8_t> buf;
        {
            logos::vectorstream write_stream(buf);
            uint64_t size = 0;
            size += logos::write(write_stream, type);
            size += logos::write(write_stream, ev.WireSize());
            size += logos::write(write_stream, origin);
            size += logos::write(write_stream, sig);
            size += logos::write(write_stream, prev);
            size += logos::write(write_stream, next);
            uint8_t count = 4;
            size += logos::write(write_stream, count);
            for(auto const & v : votes)
            {
                size += logos::write(write_stream, v.account);
                size += logos::write(write_stream, v.num_votes);
            }

            ASSERT_EQ(size,ev.WireSize());
        }

        logos::bufferstream read_stream(buf.data(), buf.size());
        bool error = false;
        ElectionVote ev2(error,read_stream);
        ASSERT_FALSE(error);
        ASSERT_EQ(ev.votes_,ev2.votes_);
        ASSERT_EQ(ev.type,ev2.type);
        ASSERT_EQ(ev.previous,ev2.previous);
        ASSERT_EQ(ev.next,ev2.next);
        ASSERT_EQ(ev,ev2);
    }
    



}
#endif // #ifdef Unit_Test_Request_Serialization
