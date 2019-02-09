#include <gtest/gtest.h>

#include <logos/token/requests.hpp>
#include <logos/request/change.hpp>
#include <logos/request/send.hpp>

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
        "fee_type": "percentage",
        "fee_rate": "5",
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
    ASSERT_EQ(issuance.fee_type, TokenFeeType::Percentage);
    ASSERT_EQ(issuance.fee_rate, 5);
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

    // Send
    //
    //
    char const * native_send = R"%%%({
        "type": "send",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
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
        "work": "0"
     })%%%";

    tree = get_tree(native_send);
    Send logos_send(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(logos_send.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(logos_send.fee.number(), 100);
    ASSERT_EQ(logos_send.sequence, 1);
    ASSERT_EQ(logos_send.transactions.size(), 3);
    ASSERT_EQ(logos_send.transactions[0].destination.to_account(),
              "lgs_1sibjaeaceh59dh7fefo49narpsoytqac5hafhujum3grnd7qrhbczfy9wx8");
    ASSERT_EQ(logos_send.transactions[0].amount, 1);
    ASSERT_EQ(logos_send.transactions[1].destination.to_account(),
              "lgs_15p6h3z7dgif1kt8skmdmo8xmobh3xyfzthoden6jqu34t6i4sgtcr4pfj5h");
    ASSERT_EQ(logos_send.transactions[1].amount, 2);
    ASSERT_EQ(logos_send.transactions[2].destination.to_account(),
              "lgs_1mkqajo9pedc1x764b5y5yzkykcm3h3hx1bumznzhgjqimjpajy9w5qfsis6");
    ASSERT_EQ(logos_send.transactions[2].amount, 3);
}

auto DoGetStreamedData = [](const auto & data, auto & buf)
{
    buf.clear();
    {
        logos::vectorstream stream(buf);
        data.ToStream(stream);
    }
};

auto GetStreamedData = [](const auto & data)
{
    std::vector<uint8_t> buf;
    DoGetStreamedData(data, buf);

    return buf;
};

auto GenerateIssuance = []()
{
    TokenIssuance issuance;

    issuance.type = RequestType::IssueTokens;
    issuance.symbol = "MYC";
    issuance.name = "MyCoin";
    issuance.total_supply = 200;
    issuance.fee_type = TokenFeeType::Flat;
    issuance.fee_rate = 10;
    issuance.settings = "1111111000";

    ControllerInfo controller;
    controller.account.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    controller.privileges = "11111111110000000000";

    issuance.controllers = {controller};
    issuance.issuer_info = "MyCoin was created by Bob";

    return issuance;
};

auto GenerateIssueAdtl = []()
{
    TokenIssueAdtl adtl;

    adtl.type = RequestType::IssueAdtlTokens;
    adtl.amount = 500;

    return adtl;
};

auto GenerateTokenChangeSetting = []()
{
    TokenChangeSetting change;

    change.type = RequestType::ChangeTokenSetting;
    change.setting = TokenSetting::AddTokens;
    change.value = SettingValue::Disabled;

    return change;
};

auto GenerateTokenImmuteSetting = []()
{
    TokenImmuteSetting immute;

    immute.type = RequestType::ImmuteTokenSetting;
    immute.setting = TokenSetting::ModifyAddTokens;

    return immute;
};

auto GenerateTokenRevoke = []()
{
    TokenRevoke revoke;

    revoke.type = RequestType::RevokeTokens;
    revoke.source.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    revoke.transaction.destination.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    revoke.transaction.amount = 500;

    return revoke;
};

auto GenerateTokenFreeze = []()
{
    TokenFreeze freeze;

    freeze.type = RequestType::FreezeTokens;
    freeze.account.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    freeze.action = FreezeAction::Freeze;

    return freeze;
};

auto GenerateTokenSetFee = []()
{
    TokenSetFee set_fee;

    set_fee.type = RequestType::SetTokenFee;
    set_fee.fee_type = TokenFeeType::Flat;
    set_fee.fee_rate = 20;

    return set_fee;
};

auto GenerateWhitelist = []()
{
    TokenWhitelist whitelist;

    whitelist.type = RequestType::UpdateWhitelist;
    whitelist.account.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");

    return whitelist;
};

auto GenerateIssuerInfo = []()
{
    TokenIssuerInfo info_a;

    info_a.type = RequestType::UpdateIssuerInfo;
    info_a.new_info = "MyCoin no longer requires whitelisting!";

    return info_a;
};

auto GenerateTokenController = []()
{
    TokenController controller;

    controller.type = RequestType::UpdateController;
    controller.action = ControllerAction::Add;
    controller.controller.account.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    controller.controller.privileges = "11111111110000000000";

    return controller;
};

auto GenerateTokenBurn = []()
{
    TokenBurn burn;

    burn.type = RequestType::BurnTokens;
    burn.amount = 1000;

    return burn;
};

auto GenerateTokenAccountSend = []()
{
    TokenAccountSend distribute;

    distribute.type = RequestType::DistributeTokens;
    distribute.transaction.destination.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    distribute.transaction.amount = 600;

    return distribute;
};

auto GenerateWithdrawFee = []()
{
    TokenAccountWithdrawFee withdraw;

    withdraw.type = RequestType::WithdrawTokens;
    withdraw.transaction.destination.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    withdraw.transaction.amount = 600;

    return withdraw;
};

auto GenerateTokenSend = []()
{
    TokenSend send_a;

    send_a.type = RequestType::SendTokens;
    send_a.transactions.resize(3);

    send_a.transactions[0].destination.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    send_a.transactions[0].amount = 600;

    send_a.transactions[1].destination.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    send_a.transactions[1].amount = 500;

    send_a.transactions[2].destination.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    send_a.transactions[2].amount = 400;

    send_a.token_fee = 20;

    return send_a;
};

TEST (Request_Serialization, stream_methods)
{
    // TokenIssuance
    //
    //
    auto issuance_a(GenerateIssuance());
    auto buf(GetStreamedData(issuance_a));

    bool error = false;
    logos::bufferstream stream(buf.data(), buf.size());
    TokenIssuance issuance_b(error, stream);

    ASSERT_FALSE(error);
    ASSERT_EQ(issuance_a.type, issuance_b.type);
    ASSERT_EQ(issuance_a.type, RequestType::IssueTokens);
    ASSERT_EQ(issuance_a.symbol, issuance_b.symbol);
    ASSERT_EQ(issuance_a.name, issuance_b.name);
    ASSERT_EQ(issuance_a.total_supply, issuance_b.total_supply);
    ASSERT_EQ(issuance_a.fee_type, issuance_b.fee_type);
    ASSERT_EQ(issuance_a.fee_rate, issuance_b.fee_rate);
    ASSERT_EQ(issuance_a.settings, issuance_b.settings);
    ASSERT_EQ(issuance_a.controllers, issuance_b.controllers);
    ASSERT_EQ(issuance_a.issuer_info, issuance_b.issuer_info);

    // Token Issue Additional
    //
    //
    auto adtl_a(GenerateIssueAdtl());
    DoGetStreamedData(adtl_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    TokenIssueAdtl adtl_b(error, stream);

    ASSERT_FALSE(error);
    ASSERT_EQ(adtl_a.type, adtl_b.type);
    ASSERT_EQ(adtl_a.type, RequestType::IssueAdtlTokens);
    ASSERT_EQ(adtl_a.amount, adtl_b.amount);
    ASSERT_EQ(adtl_a.amount, 500);

    // Token Change Setting
    //
    //
    auto change_a(GenerateTokenChangeSetting());
    DoGetStreamedData(change_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    TokenChangeSetting change_b(error, stream);

    ASSERT_FALSE(error);
    ASSERT_EQ(change_a.type, change_b.type);
    ASSERT_EQ(change_a.type, RequestType::ChangeTokenSetting);
    ASSERT_EQ(change_a.setting, change_b.setting);
    ASSERT_EQ(change_a.value, change_b.value);

    // Token Immute Setting
    //
    //
    auto immute_a(GenerateTokenImmuteSetting());
    DoGetStreamedData(immute_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    TokenImmuteSetting immute_b(error, stream);

    ASSERT_FALSE(error);
    ASSERT_EQ(immute_a.type, immute_b.type);
    ASSERT_EQ(immute_a.type, RequestType::ImmuteTokenSetting);
    ASSERT_EQ(immute_a.setting, immute_b.setting);

    // Token Revoke
    //
    //
    auto revoke_a(GenerateTokenRevoke());
    DoGetStreamedData(revoke_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    TokenRevoke revoke_b(error, stream);

    ASSERT_FALSE(error);
    ASSERT_EQ(revoke_a.type, revoke_b.type);
    ASSERT_EQ(revoke_a.type, RequestType::RevokeTokens);
    ASSERT_EQ(revoke_a.source, revoke_b.source);
    ASSERT_EQ(revoke_a.transaction, revoke_b.transaction);
    ASSERT_EQ(revoke_a.transaction.amount, 500);

    // Token Freeze
    //
    //
    auto freeze_a(GenerateTokenFreeze());
    DoGetStreamedData(freeze_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    TokenFreeze freeze_b(error, stream);

    ASSERT_FALSE(error);
    ASSERT_EQ(freeze_a.type, freeze_b.type);
    ASSERT_EQ(freeze_a.type, RequestType::FreezeTokens);
    ASSERT_EQ(freeze_a.account, freeze_b.account);
    ASSERT_EQ(freeze_a.action, freeze_b.action);

    // Token Set Fee
    //
    //
    auto set_fee_a(GenerateTokenSetFee());
    DoGetStreamedData(set_fee_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    TokenSetFee set_fee_b(error, stream);

    ASSERT_FALSE(error);
    ASSERT_EQ(set_fee_a.type, set_fee_b.type);
    ASSERT_EQ(set_fee_a.type, RequestType::SetTokenFee);
    ASSERT_EQ(set_fee_a.fee_type, set_fee_b.fee_type);
    ASSERT_EQ(set_fee_a.fee_rate, set_fee_b.fee_rate);

    // Token Whitelist
    //
    //
    auto whitelist_a(GenerateWhitelist());
    DoGetStreamedData(whitelist_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    TokenWhitelist whitelist_b(error, stream);

    ASSERT_FALSE(error);
    ASSERT_EQ(whitelist_a.type, whitelist_b.type);
    ASSERT_EQ(whitelist_a.type, RequestType::UpdateWhitelist);
    ASSERT_EQ(whitelist_a.account, whitelist_b.account);

    // Token Issuer Info
    //
    //
    auto info_a(GenerateIssuerInfo());
    DoGetStreamedData(info_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    TokenIssuerInfo info_b(error, stream);

    ASSERT_FALSE(error);
    ASSERT_EQ(info_a.type, info_b.type);
    ASSERT_EQ(info_a.type, RequestType::UpdateIssuerInfo);
    ASSERT_EQ(info_a.new_info, info_b.new_info);

    // Token Controller
    //
    //
    auto controller_a(GenerateTokenController());
    DoGetStreamedData(controller_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    TokenController controller_b(error, stream);

    ASSERT_FALSE(error);
    ASSERT_EQ(controller_a.type, controller_b.type);
    ASSERT_EQ(controller_a.type, RequestType::UpdateController);
    ASSERT_EQ(controller_a.action, controller_b.action);
    ASSERT_EQ(controller_a.controller, controller_b.controller);

    // Token Burn
    //
    //
    auto burn_a(GenerateTokenBurn());
    DoGetStreamedData(burn_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    TokenBurn burn_b(error, stream);

    ASSERT_FALSE(error);
    ASSERT_EQ(burn_a.type, burn_b.type);
    ASSERT_EQ(burn_a.type, RequestType::BurnTokens);
    ASSERT_EQ(burn_a.amount, burn_b.amount);
    ASSERT_EQ(burn_a.amount, 1000);

    // Token Account Send
    //
    //
    auto distribute_a(GenerateTokenAccountSend());
    DoGetStreamedData(distribute_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    TokenAccountSend distribute_b(error, stream);

    ASSERT_FALSE(error);
    ASSERT_EQ(distribute_a.type, distribute_b.type);
    ASSERT_EQ(distribute_a.type, RequestType::DistributeTokens);
    ASSERT_EQ(distribute_a.transaction, distribute_b.transaction);

    // Withdraw Fee
    //
    //
    auto withdraw_a(GenerateWithdrawFee());
    DoGetStreamedData(withdraw_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    TokenAccountWithdrawFee withdraw_b(error, stream);

    ASSERT_FALSE(error);
    ASSERT_EQ(withdraw_a.type, withdraw_b.type);
    ASSERT_EQ(withdraw_a.type, RequestType::WithdrawTokens);
    ASSERT_EQ(withdraw_a.transaction, withdraw_b.transaction);

    // Token Send
    //
    //
    auto send_a(GenerateTokenSend());
    DoGetStreamedData(send_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    TokenSend send_b(error, stream);

    ASSERT_FALSE(error);
    ASSERT_EQ(send_a.type, send_b.type);
    ASSERT_EQ(send_a.type, RequestType::SendTokens);
    ASSERT_EQ(send_a.transactions, send_b.transactions);
    ASSERT_EQ(send_a.token_fee, send_b.token_fee);
    ASSERT_EQ(send_a.token_fee, 20);
}

TEST (Request_Serialization, database_methods)
{
    // TokenIssuance
    //
    //
    auto issuance_a(GenerateIssuance());

    std::vector<uint8_t> buf;

    bool error = false;
    TokenIssuance issuance_b(error,
                             issuance_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(issuance_a.type, issuance_b.type);
    ASSERT_EQ(issuance_a.type, RequestType::IssueTokens);
    ASSERT_EQ(issuance_a.symbol, issuance_b.symbol);
    ASSERT_EQ(issuance_a.name, issuance_b.name);
    ASSERT_EQ(issuance_a.total_supply, issuance_b.total_supply);
    ASSERT_EQ(issuance_a.fee_type, issuance_b.fee_type);
    ASSERT_EQ(issuance_a.fee_rate, issuance_b.fee_rate);
    ASSERT_EQ(issuance_a.settings, issuance_b.settings);
    ASSERT_EQ(issuance_a.controllers, issuance_b.controllers);
    ASSERT_EQ(issuance_a.issuer_info, issuance_b.issuer_info);

    // Token Issue Additional
    //
    //
    auto adtl_a(GenerateIssueAdtl());
    DoGetStreamedData(adtl_a, buf);

    buf.clear();

    error = false;
    TokenIssueAdtl adtl_b(error,
                          adtl_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(adtl_a.type, adtl_b.type);
    ASSERT_EQ(adtl_a.type, RequestType::IssueAdtlTokens);
    ASSERT_EQ(adtl_a.amount, adtl_b.amount);
    ASSERT_EQ(adtl_a.amount, 500);

    // Token Change Setting
    //
    //
    auto change_a(GenerateTokenChangeSetting());
    DoGetStreamedData(change_a, buf);

    buf.clear();

    error = false;
    TokenChangeSetting change_b(error,
                                change_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(change_a.type, change_b.type);
    ASSERT_EQ(change_a.type, RequestType::ChangeTokenSetting);
    ASSERT_EQ(change_a.setting, change_b.setting);
    ASSERT_EQ(change_a.value, change_b.value);

    // Token Immute Setting
    //
    //
    auto immute_a(GenerateTokenImmuteSetting());
    DoGetStreamedData(immute_a, buf);

    buf.clear();

    error = false;
    TokenImmuteSetting immute_b(error,
                                immute_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(immute_a.type, immute_b.type);
    ASSERT_EQ(immute_a.type, RequestType::ImmuteTokenSetting);
    ASSERT_EQ(immute_a.setting, immute_b.setting);

    // Token Revoke
    //
    //
    auto revoke_a(GenerateTokenRevoke());
    DoGetStreamedData(revoke_a, buf);

    buf.clear();

    error = false;
    TokenRevoke revoke_b(error,
                         revoke_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(revoke_a.type, revoke_b.type);
    ASSERT_EQ(revoke_a.type, RequestType::RevokeTokens);
    ASSERT_EQ(revoke_a.source, revoke_b.source);
    ASSERT_EQ(revoke_a.transaction, revoke_b.transaction);
    ASSERT_EQ(revoke_a.transaction.amount, 500);


    // Token Freeze
    //
    //
    auto freeze_a(GenerateTokenFreeze());
    DoGetStreamedData(freeze_a, buf);

    buf.clear();

    error = false;
    TokenFreeze freeze_b(error,
                         freeze_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(freeze_a.type, freeze_b.type);
    ASSERT_EQ(freeze_a.type, RequestType::FreezeTokens);
    ASSERT_EQ(freeze_a.account, freeze_b.account);
    ASSERT_EQ(freeze_a.action, freeze_b.action);

    // Token Set Fee
    //
    //
    auto set_fee_a(GenerateTokenSetFee());
    DoGetStreamedData(set_fee_a, buf);

    buf.clear();

    error = false;
    TokenSetFee set_fee_b(error,
                          set_fee_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(set_fee_a.type, set_fee_b.type);
    ASSERT_EQ(set_fee_a.type, RequestType::SetTokenFee);
    ASSERT_EQ(set_fee_a.fee_type, set_fee_b.fee_type);
    ASSERT_EQ(set_fee_a.fee_rate, set_fee_b.fee_rate);

    // Token Whitelist
    //
    //
    auto whitelist_a(GenerateWhitelist());
    DoGetStreamedData(whitelist_a, buf);

    buf.clear();

    error = false;
    TokenWhitelist whitelist_b(error,
                               whitelist_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(whitelist_a.type, whitelist_b.type);
    ASSERT_EQ(whitelist_a.type, RequestType::UpdateWhitelist);
    ASSERT_EQ(whitelist_a.account, whitelist_b.account);

    // Token Issuer Info
    //
    //
    auto info_a(GenerateIssuerInfo());
    DoGetStreamedData(info_a, buf);

    buf.clear();

    error = false;
    TokenIssuerInfo info_b(error,
                           info_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(info_a.type, info_b.type);
    ASSERT_EQ(info_a.type, RequestType::UpdateIssuerInfo);
    ASSERT_EQ(info_a.new_info, info_b.new_info);

    // Token Controller
    //
    //
    auto controller_a(GenerateTokenController());
    DoGetStreamedData(controller_a, buf);

    buf.clear();

    error = false;
    TokenController controller_b(error,
                                 controller_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(controller_a.type, controller_b.type);
    ASSERT_EQ(controller_a.type, RequestType::UpdateController);
    ASSERT_EQ(controller_a.action, controller_b.action);
    ASSERT_EQ(controller_a.controller, controller_b.controller);

    // Token Burn
    //
    //
    auto burn_a(GenerateTokenBurn());
    DoGetStreamedData(burn_a, buf);

    buf.clear();

    error = false;
    TokenBurn burn_b(error,
                     burn_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(burn_a.type, burn_b.type);
    ASSERT_EQ(burn_a.type, RequestType::BurnTokens);
    ASSERT_EQ(burn_a.amount, burn_b.amount);
    ASSERT_EQ(burn_a.amount, 1000);

    // Token Account Send
    //
    //
    auto distribute_a(GenerateTokenAccountSend());
    DoGetStreamedData(distribute_a, buf);

    buf.clear();

    error = false;
    TokenAccountSend distribute_b(error,
                                  distribute_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(distribute_a.type, distribute_b.type);
    ASSERT_EQ(distribute_a.type, RequestType::DistributeTokens);
    ASSERT_EQ(distribute_a.transaction, distribute_b.transaction);

    // Withdraw Fee
    //
    //
    auto withdraw_a(GenerateWithdrawFee());
    DoGetStreamedData(withdraw_a, buf);

    buf.clear();

    error = false;
    TokenAccountWithdrawFee withdraw_b(error,
                                       withdraw_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(withdraw_a.type, withdraw_b.type);
    ASSERT_EQ(withdraw_a.type, RequestType::WithdrawTokens);
    ASSERT_EQ(withdraw_a.transaction, withdraw_b.transaction);

    // Token Send
    //
    //
    auto send_a(GenerateTokenSend());
    DoGetStreamedData(send_a, buf);

    buf.clear();

    error = false;
    TokenSend send_b(error,
                     send_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(send_a.type, send_b.type);
    ASSERT_EQ(send_a.type, RequestType::SendTokens);
    ASSERT_EQ(send_a.transactions, send_b.transactions);
    ASSERT_EQ(send_a.token_fee, send_b.token_fee);
    ASSERT_EQ(send_a.token_fee, 20);
}

#endif // #ifdef Unit_Test_Request_Serialization