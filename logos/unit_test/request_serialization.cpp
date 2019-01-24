#include <gtest/gtest.h>

#include <logos/token/requests.hpp>
#include <logos/request/change.hpp>

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
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "admin_account": "lgs_13c87hr61uu1icjinoxuhrphcj4pb3se8hpeggscqfzwk1de5pd8y6opd767",
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
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "admin_account": "lgs_13c87hr61uu1icjinoxuhrphcj4pb3se8hpeggscqfzwk1de5pd8y6opd767",
        "amount": "50000"
     })%%%";

    tree = get_tree(token_issue_adtl);
    TokenIssueAdtl issue_adtl(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(issue_adtl.amount, 50000);

    // Token Change Setting
    //
    //
    char const * token_change_setting = R"%%%({
        "type": "issue",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "admin_account": "lgs_13c87hr61uu1icjinoxuhrphcj4pb3se8hpeggscqfzwk1de5pd8y6opd767",
        "setting": "FREEZE",
        "value": "true"
     })%%%";

    tree = get_tree(token_change_setting);
    TokenChangeSetting change_setting(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(change_setting.setting, TokenSetting::Freeze);
    ASSERT_EQ(change_setting.value, SettingValue::Enabled);

    // Token Immute Setting
    //
    //
    char const * token_immute_setting = R"%%%({
        "type": "issue",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "admin_account": "lgs_13c87hr61uu1icjinoxuhrphcj4pb3se8hpeggscqfzwk1de5pd8y6opd767",
        "setting": "modify_freeze"
     })%%%";

    tree = get_tree(token_immute_setting);
    TokenImmuteSetting immute_setting(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(immute_setting.setting, TokenSetting::ModifyFreeze);

    // Token Revoke
    //
    //
    char const * token_revoke = R"%%%({
        "type": "issue",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "admin_account": "lgs_13c87hr61uu1icjinoxuhrphcj4pb3se8hpeggscqfzwk1de5pd8y6opd767",
        "source": "lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju",
        "destination": "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz",
        "amount": "50"
     })%%%";

    tree = get_tree(token_revoke);
    TokenRevoke revoke(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(revoke.source.to_account(), "lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    ASSERT_EQ(revoke.destination.to_account(), "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz");
    ASSERT_EQ(revoke.amount, 50);

    // Token Freeze
    //
    //
    char const * token_freeze = R"%%%({
        "type": "issue",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "admin_account": "lgs_13c87hr61uu1icjinoxuhrphcj4pb3se8hpeggscqfzwk1de5pd8y6opd767",
        "account": "lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju",
        "action": "unfreeze"
     })%%%";

    tree = get_tree(token_freeze);
    TokenFreeze freeze(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(freeze.account.to_account(), "lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    ASSERT_EQ(freeze.action, FreezeAction::Unfreeze);

    // Token Set Fee
    //
    //
    char const * token_set_fee = R"%%%({
        "type": "issue",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "admin_account": "lgs_13c87hr61uu1icjinoxuhrphcj4pb3se8hpeggscqfzwk1de5pd8y6opd767",
        "fee_type": "percentage",
        "fee_rate": "10"
     })%%%";

    tree = get_tree(token_set_fee);
    TokenSetFee set_fee(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(set_fee.fee_type, TokenFeeType::Percentage);
    ASSERT_EQ(set_fee.fee_rate, 10);

    // Token Whitelist
    //
    //
    char const * token_whitelist = R"%%%({
        "type": "issue",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "admin_account": "lgs_13c87hr61uu1icjinoxuhrphcj4pb3se8hpeggscqfzwk1de5pd8y6opd767",
        "account": "lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju"
     })%%%";

    tree = get_tree(token_whitelist);
    TokenWhitelist whitelist(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(whitelist.account.to_account(), "lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");

    // Token Issuer Info
    //
    //
    char const * token_issuer_info = R"%%%({
        "type": "issue",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "admin_account": "lgs_13c87hr61uu1icjinoxuhrphcj4pb3se8hpeggscqfzwk1de5pd8y6opd767",
        "new_info": "This is new info"
     })%%%";

    tree = get_tree(token_issuer_info);
    TokenIssuerInfo issuer_info(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(issuer_info.new_info, "This is new info");

    // Token Controller
    //
    //
    char const * token_controller = R"%%%({
        "type": "issue",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "admin_account": "lgs_13c87hr61uu1icjinoxuhrphcj4pb3se8hpeggscqfzwk1de5pd8y6opd767",
        "action": "add",
        "controller": {
            "account": "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz",
            "privileges": ["change_add", "withdraw_fee"]
        }
     })%%%";

    tree = get_tree(token_controller);
    TokenController controller(error, tree);

    ASSERT_FALSE(error);
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
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "admin_account": "lgs_13c87hr61uu1icjinoxuhrphcj4pb3se8hpeggscqfzwk1de5pd8y6opd767",
        "amount": "1000"
     })%%%";

    tree = get_tree(token_burn);
    TokenBurn burn(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(burn.amount, 1000);

    // Token Account Send
    //
    //
    char const * token_account_send = R"%%%({
        "type": "issue",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "admin_account": "lgs_13c87hr61uu1icjinoxuhrphcj4pb3se8hpeggscqfzwk1de5pd8y6opd767",
        "destination": "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz",
        "amount": "100"
     })%%%";

    tree = get_tree(token_account_send);
    TokenAccountSend account_send(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(account_send.destination.to_account(),
              "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz");
    ASSERT_EQ(account_send.amount, 100);

    // Token Account Withdraw Fee
    //
    //
    char const * token_account_withdraw_fee = R"%%%({
        "type": "issue",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "admin_account": "lgs_13c87hr61uu1icjinoxuhrphcj4pb3se8hpeggscqfzwk1de5pd8y6opd767",
        "source": "lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju",
        "destination": "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz",
        "amount": "50"
     })%%%";

    tree = get_tree(token_account_withdraw_fee);
    TokenAccountWithdrawFee withdraw_fee(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(withdraw_fee.destination.to_account(),
              "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz");
    ASSERT_EQ(withdraw_fee.amount, 50);

    // Token Send
    //
    //
    char const * token_send = R"%%%({
        "type": "issue",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "admin_account": "lgs_13c87hr61uu1icjinoxuhrphcj4pb3se8hpeggscqfzwk1de5pd8y6opd767",
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
        "fee": "5"
     })%%%";

    tree = get_tree(token_send);
    TokenSend send(error, tree);

    ASSERT_FALSE(error);
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
    ASSERT_EQ(send.fee, 5);

    // Change Representative
    //
    //
    char const * native_change = R"%%%({
        "type": "change",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "client": "lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju",
        "representative": "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz"
     })%%%";

    tree = get_tree(native_change);
    Change change(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(change.client.to_account(),
              "lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    ASSERT_EQ(change.representative.to_account(),
              "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz");
}

#endif // #ifdef Unit_Test_Request_Serialization