#include <gtest/gtest.h>

#include <logos/request/utility.hpp>
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

    // Issuance
    //
    //
    char const * issuance_json = R"%%%({
        "type": "issuance",
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
        "settings": ["issuance", "modify_issuance", "whitelist"],
        "controllers": [
            {
                "account": "lgs_19bxabqmra8ijd8s3qs3u611z5wss6amnem4bht6u9e3odpfper7ed1i7yds",
                "privileges": [
                    "change_issuance",
                    "change_revoke"
                ]
            },
            {
                "account": "lgs_3dwpb16qw5eh6yt5c3waobn9y113pg6epnsbiy8uo3c5q3m5onpbye1u8tw6",
                "privileges": [
                    "change_issuance",
                    "change_revoke",
                    "change_freeze",
                    "withdraw_fee"
                ]
            }
        ],
        "issuer_info": "MyCoin is a coin owned by me."
     })%%%";

    boost::property_tree::ptree tree = get_tree(issuance_json);
    bool error = false;
    Issuance issuance(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(issuance.type, RequestType::Issuance);
    ASSERT_EQ(issuance.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(issuance.fee.number(), 100);
    ASSERT_EQ(issuance.sequence, 1);
    ASSERT_EQ(issuance.symbol, "MYC");
    ASSERT_EQ(issuance.total_supply, 65000);
    ASSERT_EQ(issuance.fee_type, TokenFeeType::Percentage);
    ASSERT_EQ(issuance.fee_rate, 5);
    ASSERT_EQ(issuance.controllers.size(), 2);
    ASSERT_TRUE(issuance.settings[size_t(TokenSetting::Issuance)]);
    ASSERT_FALSE(issuance.settings[size_t(TokenSetting::ModifyWhitelist)]);
    ASSERT_TRUE(issuance.controllers[1].privileges[size_t(ControllerPrivilege::ChangeFreeze)]);

    // Issue Additional
    //
    //
    char const * issue_adtl_json = R"%%%({
        "type": "issue_additional",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "amount": "50000"
     })%%%";

    tree = get_tree(issue_adtl_json);
    IssueAdditional issue_adtl(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(issue_adtl.type, RequestType::IssueAdditional);
    ASSERT_EQ(issue_adtl.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(issue_adtl.fee.number(), 100);
    ASSERT_EQ(issue_adtl.sequence, 1);
    ASSERT_EQ(issue_adtl.amount, 50000);

    // Change Setting
    //
    //
    char const * change_setting_json = R"%%%({
        "type": "change_setting",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "setting": "freeze",
        "value": "true"
     })%%%";

    tree = get_tree(change_setting_json);
    ChangeSetting change_setting(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(change_setting.type, RequestType::ChangeSetting);
    ASSERT_EQ(change_setting.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(change_setting.fee.number(), 100);
    ASSERT_EQ(change_setting.sequence, 1);
    ASSERT_EQ(change_setting.setting, TokenSetting::Freeze);
    ASSERT_EQ(change_setting.value, SettingValue::Enabled);

    // Immute Setting
    //
    //
    char const * immute_setting_json = R"%%%({
        "type": "immute_setting",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "setting": "modify_freeze"
     })%%%";

    tree = get_tree(immute_setting_json);
    ImmuteSetting immute_setting(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(immute_setting.type, RequestType::ImmuteSetting);
    ASSERT_EQ(immute_setting.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(immute_setting.fee.number(), 100);
    ASSERT_EQ(immute_setting.sequence, 1);
    ASSERT_EQ(immute_setting.setting, TokenSetting::ModifyFreeze);

    // Revoke
    //
    //
    char const * revoke_json = R"%%%({
        "type": "revoke",
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

    tree = get_tree(revoke_json);
    Revoke revoke(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(revoke.type, RequestType::Revoke);
    ASSERT_EQ(revoke.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(revoke.fee.number(), 100);
    ASSERT_EQ(revoke.sequence, 1);
    ASSERT_EQ(revoke.source.to_account(), "lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    ASSERT_EQ(revoke.transaction.destination.to_account(),
              "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz");
    ASSERT_EQ(revoke.transaction.amount, 50);

    // Adjust User Status
    //
    //
    char const * adjust_status_json = R"%%%({
        "type": "adjust_user_status",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "account": "lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju",
        "status": "unfrozen"
     })%%%";

    tree = get_tree(adjust_status_json);
    AdjustUserStatus adjust_status(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(adjust_status.type, RequestType::AdjustUserStatus);
    ASSERT_EQ(adjust_status.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(adjust_status.fee.number(), 100);
    ASSERT_EQ(adjust_status.sequence, 1);
    ASSERT_EQ(adjust_status.account.to_account(), "lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    ASSERT_EQ(adjust_status.status, UserStatus::Unfrozen);

    // Adjust Fee
    //
    //
    char const * adjust_fee_json = R"%%%({
        "type": "adjust_fee",
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

    tree = get_tree(adjust_fee_json);
    AdjustFee adjust_fee(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(adjust_fee.type, RequestType::AdjustFee);
    ASSERT_EQ(adjust_fee.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(adjust_fee.fee.number(), 100);
    ASSERT_EQ(adjust_fee.sequence, 1);
    ASSERT_EQ(adjust_fee.fee_type, TokenFeeType::Percentage);
    ASSERT_EQ(adjust_fee.fee_rate, 10);

    // Update Issuer Info
    //
    //
    char const * issuer_info_json = R"%%%({
        "type": "update_issuer_info",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "new_info": "This is new info"
     })%%%";

    tree = get_tree(issuer_info_json);
    UpdateIssuerInfo issuer_info(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(issuer_info.type, RequestType::UpdateIssuerInfo);
    ASSERT_EQ(issuer_info.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(issuer_info.fee.number(), 100);
    ASSERT_EQ(issuer_info.sequence, 1);
    ASSERT_EQ(issuer_info.new_info, "This is new info");

    // Update Controller
    //
    //
    char const * controller_json = R"%%%({
        "type": "update_controller",
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
            "privileges": ["change_issuance", "withdraw_fee"]
        }
     })%%%";

    tree = get_tree(controller_json);
    UpdateController controller(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(controller.type, RequestType::UpdateController);
    ASSERT_EQ(controller.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(controller.fee.number(), 100);
    ASSERT_EQ(controller.sequence, 1);
    ASSERT_EQ(controller.action, ControllerAction::Add);
    ASSERT_EQ(controller.controller.account.to_account(), "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz");
    ASSERT_TRUE(controller.controller.privileges[size_t(ControllerPrivilege::ChangeIssuance)]);
    ASSERT_TRUE(controller.controller.privileges[size_t(ControllerPrivilege::WithdrawFee)]);
    ASSERT_FALSE(controller.controller.privileges[size_t(ControllerPrivilege::UpdateController)]);
    ASSERT_FALSE(controller.controller.privileges[size_t(ControllerPrivilege::Revoke)]);
    ASSERT_FALSE(controller.controller.privileges[size_t(ControllerPrivilege::AdjustFee)]);

    // Burn
    //
    //
    char const * burn_json = R"%%%({
        "type": "burn",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "100",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "amount": "1000"
     })%%%";

    tree = get_tree(burn_json);
    Burn burn(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(burn.type, RequestType::Burn);
    ASSERT_EQ(burn.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(burn.fee.number(), 100);
    ASSERT_EQ(burn.sequence, 1);
    ASSERT_EQ(burn.amount, 1000);

    // Distribute
    //
    //
    char const * distribute_json = R"%%%({
        "type": "distribute",
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

    tree = get_tree(distribute_json);
    Distribute distribute(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(distribute.type, RequestType::Distribute);
    ASSERT_EQ(distribute.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(distribute.fee.number(), 100);
    ASSERT_EQ(distribute.sequence, 1);
    ASSERT_EQ(distribute.transaction.destination.to_account(),
              "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz");
    ASSERT_EQ(distribute.transaction.amount, 100);

    // Token Account Withdraw Fee
    //
    //
    char const * withdraw_fee_json = R"%%%({
        "type": "withdraw_fee",
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

    tree = get_tree(withdraw_fee_json);
    WithdrawFee withdraw_fee(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(withdraw_fee.type, RequestType::WithdrawFee);
    ASSERT_EQ(withdraw_fee.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(withdraw_fee.fee.number(), 100);
    ASSERT_EQ(withdraw_fee.sequence, 1);
    ASSERT_EQ(withdraw_fee.transaction.destination.to_account(),
              "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz");
    ASSERT_EQ(withdraw_fee.transaction.amount, 50);

    // Token Send
    //
    //
    char const * token_send_json = R"%%%({
        "type": "token_send",
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

    tree = get_tree(token_send_json);
    TokenSend token_send(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(token_send.type, RequestType::TokenSend);
    ASSERT_EQ(token_send.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(token_send.fee.number(), 100);
    ASSERT_EQ(token_send.sequence, 1);
    ASSERT_EQ(token_send.transactions.size(), 3);
    ASSERT_EQ(token_send.transactions[0].destination.to_account(),
              "lgs_1sibjaeaceh59dh7fefo49narpsoytqac5hafhujum3grnd7qrhbczfy9wx8");
    ASSERT_EQ(token_send.transactions[0].amount, 1);
    ASSERT_EQ(token_send.transactions[1].destination.to_account(),
              "lgs_15p6h3z7dgif1kt8skmdmo8xmobh3xyfzthoden6jqu34t6i4sgtcr4pfj5h");
    ASSERT_EQ(token_send.transactions[1].amount, 2);
    ASSERT_EQ(token_send.transactions[2].destination.to_account(),
              "lgs_1mkqajo9pedc1x764b5y5yzkykcm3h3hx1bumznzhgjqimjpajy9w5qfsis6");
    ASSERT_EQ(token_send.transactions[2].amount, 3);
    ASSERT_EQ(token_send.token_fee, 5);

    // Change Representative
    //
    //
    char const * change_json = R"%%%({
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

    tree = get_tree(change_json);
    Change change(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(change.type, RequestType::Change);
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
    char const * logos_send_json = R"%%%({
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

    tree = get_tree(logos_send_json);
    Send logos_send(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(logos_send.type, RequestType::Send);
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
    Issuance issuance;

    issuance.type = RequestType::Issuance;
    issuance.symbol = "MYC";
    issuance.name = "MyCoin";
    issuance.total_supply = 200;
    issuance.fee_type = TokenFeeType::Flat;
    issuance.fee_rate = 10;
    issuance.settings = "1111111000";
    issuance.controllers =
        {
            {
                {"lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju"},
                {"11111111110000000000"}
            },
            {
                {"lgs_15p6h3z7dgif1kt8skmdmo8xmobh3xyfzthoden6jqu34t6i4sgtcr4pfj5h"},
                {"11111111110000100100"}
            }
        };
    issuance.issuer_info = "MyCoin was created by Bob";

    return issuance;
};

auto GenerateIssueAdtl = []()
{
    IssueAdditional adtl;

    adtl.type = RequestType::IssueAdditional;
    adtl.amount = 500;

    return adtl;
};

auto GenerateChangeSetting = []()
{
    ChangeSetting change;

    change.type = RequestType::ChangeSetting;
    change.setting = TokenSetting::Issuance;
    change.value = SettingValue::Disabled;

    return change;
};

auto GenerateImmuteSetting = []()
{
    ImmuteSetting immute;

    immute.type = RequestType::ImmuteSetting;
    immute.setting = TokenSetting::ModifyIssuance;

    return immute;
};

auto GenerateRevoke = []()
{
    Revoke revoke;

    revoke.type = RequestType::Revoke;
    revoke.source.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    revoke.transaction.destination.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    revoke.transaction.amount = 500;

    return revoke;
};

auto GenerateAdjustUserStatus = []()
{
    AdjustUserStatus adjust_status;

    adjust_status.type = RequestType::AdjustUserStatus;
    adjust_status.account.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    adjust_status.status = UserStatus::Frozen;

    return adjust_status;
};

auto GenerateAdjustFee = []()
{
    AdjustFee adjust_fee;

    adjust_fee.type = RequestType::AdjustFee;
    adjust_fee.fee_type = TokenFeeType::Flat;
    adjust_fee.fee_rate = 20;

    return adjust_fee;
};

auto GenerateIssuerInfo = []()
{
    UpdateIssuerInfo info_a;

    info_a.type = RequestType::UpdateIssuerInfo;
    info_a.new_info = "MyCoin no longer requires whitelisting!";

    return info_a;
};

auto GenerateUpdateController = []()
{
    UpdateController controller;

    controller.type = RequestType::UpdateController;
    controller.action = ControllerAction::Add;
    controller.controller.account.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    controller.controller.privileges = "11111111110000000000";

    return controller;
};

auto GenerateBurn = []()
{
    Burn burn;

    burn.type = RequestType::Burn;
    burn.amount = 1000;

    return burn;
};

auto GenerateDistribute = []()
{
    Distribute distribute;

    distribute.type = RequestType::Distribute;
    distribute.transaction.destination.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    distribute.transaction.amount = 600;

    return distribute;
};

auto GenerateWithdrawFee = []()
{
    WithdrawFee withdraw;

    withdraw.type = RequestType::WithdrawFee;
    withdraw.transaction.destination.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    withdraw.transaction.amount = 600;

    return withdraw;
};

auto GenerateTokenSend = []()
{
    TokenSend send;

    send.type = RequestType::TokenSend;
    send.transactions.resize(3);

    send.transactions[0].destination.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    send.transactions[0].amount = 600;

    send.transactions[1].destination.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    send.transactions[1].amount = 500;

    send.transactions[2].destination.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    send.transactions[2].amount = 400;

    send.token_fee = 20;

    return send;
};

template<typename RequestType, typename ...Args>
RequestType GetRequest(Args&& ...args)
{
    return *static_pointer_cast<RequestType>(DeserializeRequest(args...));
}

TEST (Request_Serialization, stream_methods)
{
    // Issuance
    //
    //
    auto issuance_a(GenerateIssuance());
    auto buf(GetStreamedData(issuance_a));

    bool error = false;
    logos::bufferstream stream(buf.data(), buf.size());
    Issuance issuance_b(GetRequest<Issuance>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(issuance_a, issuance_b);

    // Issue Additional
    //
    //
    auto adtl_a(GenerateIssueAdtl());
    DoGetStreamedData(adtl_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    IssueAdditional adtl_b(GetRequest<IssueAdditional>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(adtl_a, adtl_b);

    // Change Setting
    //
    //
    auto change_a(GenerateChangeSetting());
    DoGetStreamedData(change_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    ChangeSetting change_b(GetRequest<ChangeSetting>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(change_a, change_b);

    // Immute Setting
    //
    //
    auto immute_a(GenerateImmuteSetting());
    DoGetStreamedData(immute_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    ImmuteSetting immute_b(GetRequest<ImmuteSetting>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(immute_a, immute_b);

    // Revoke
    //
    //
    auto revoke_a(GenerateRevoke());
    DoGetStreamedData(revoke_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    Revoke revoke_b(GetRequest<Revoke>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(revoke_a, revoke_b);

    // Adjust User Status
    //
    //
    auto adjust_status_a(GenerateAdjustUserStatus());
    DoGetStreamedData(adjust_status_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    AdjustUserStatus adjust_status_b(GetRequest<AdjustUserStatus>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(adjust_status_a, adjust_status_b);

    // Adjust Fee
    //
    //
    auto adjust_fee_a(GenerateAdjustFee());
    DoGetStreamedData(adjust_fee_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    AdjustFee adjust_fee_b(GetRequest<AdjustFee>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(adjust_fee_a, adjust_fee_b);

    // Update Issuer Info
    //
    //
    auto info_a(GenerateIssuerInfo());
    DoGetStreamedData(info_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    UpdateIssuerInfo info_b(GetRequest<UpdateIssuerInfo>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(info_a, info_b);

    // Update Controller
    //
    //
    auto controller_a(GenerateUpdateController());
    DoGetStreamedData(controller_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    UpdateController controller_b(GetRequest<UpdateController>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(controller_a, controller_b);

    // Burn
    //
    //
    auto burn_a(GenerateBurn());
    DoGetStreamedData(burn_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    Burn burn_b(GetRequest<Burn>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(burn_a, burn_b);

    // Distribute
    //
    //
    auto distribute_a(GenerateDistribute());
    DoGetStreamedData(distribute_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    Distribute distribute_b(GetRequest<Distribute>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(distribute_a, distribute_b);

    // Withdraw Fee
    //
    //
    auto withdraw_a(GenerateWithdrawFee());
    DoGetStreamedData(withdraw_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    WithdrawFee withdraw_b(GetRequest<WithdrawFee>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(withdraw_a, withdraw_b);

    // Token Send
    //
    //
    auto send_a(GenerateTokenSend());
    DoGetStreamedData(send_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    TokenSend send_b(GetRequest<TokenSend>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(send_a, send_b);
}

TEST (Request_Serialization, database_methods)
{
    // Issuance
    //
    //
    auto issuance_a(GenerateIssuance());

    std::vector<uint8_t> buf;

    bool error = false;
    Issuance issuance_b(error,
                        issuance_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(issuance_a, issuance_b);

    // Issue Additional
    //
    //
    auto adtl_a(GenerateIssueAdtl());

    buf.clear();

    error = false;
    IssueAdditional adtl_b(error,
                           adtl_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(adtl_a, adtl_b);

    // Change Setting
    //
    //
    auto change_a(GenerateChangeSetting());

    buf.clear();

    error = false;
    ChangeSetting change_b(error,
                           change_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(change_a, change_b);

    // Immute Setting
    //
    //
    auto immute_a(GenerateImmuteSetting());

    buf.clear();

    error = false;
    ImmuteSetting immute_b(error,
                           immute_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(immute_a, immute_b);

    // Revoke
    //
    //
    auto revoke_a(GenerateRevoke());

    buf.clear();

    error = false;
    Revoke revoke_b(error,
                    revoke_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(revoke_a, revoke_b);

    // Adjust User Status
    //
    //
    auto adjust_status_a(GenerateAdjustUserStatus());

    buf.clear();

    error = false;
    AdjustUserStatus adjust_status_b(error,
                                     adjust_status_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(adjust_status_a, adjust_status_b);

    // Adjust Fee
    //
    //
    auto adjust_fee_a(GenerateAdjustFee());

    buf.clear();

    error = false;
    AdjustFee adjust_fee_b(error,
                           adjust_fee_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(adjust_fee_a, adjust_fee_b);

    // Update Issuer Info
    //
    //
    auto info_a(GenerateIssuerInfo());

    buf.clear();

    error = false;
    UpdateIssuerInfo info_b(error,
                            info_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(info_a, info_b);

    // Update Controller
    //
    //
    auto controller_a(GenerateUpdateController());

    buf.clear();

    error = false;
    UpdateController controller_b(error,
                                  controller_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(controller_a, controller_b);

    // Burn
    //
    //
    auto burn_a(GenerateBurn());

    buf.clear();

    error = false;
    Burn burn_b(error,
                burn_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(burn_a, burn_b);

    // Distribute
    //
    //
    auto distribute_a(GenerateDistribute());

    buf.clear();

    error = false;
    Distribute distribute_b(error,
                            distribute_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(distribute_a, distribute_b);

    // Withdraw Fee
    //
    //
    auto withdraw_a(GenerateWithdrawFee());

    buf.clear();

    error = false;
    WithdrawFee withdraw_b(error,
                           withdraw_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(withdraw_a, withdraw_b);

    // Token Send
    //
    //
    auto send_a(GenerateTokenSend());

    buf.clear();

    error = false;
    TokenSend send_b(error,
                     send_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(send_a, send_b);
}

TEST (Request_Serialization, json_serialization)
{
    // Issuance
    //
    //
    auto issuance_a(GenerateIssuance());

    bool error = false;
    Issuance issuance_b(error,
                        issuance_a.SerializeJson());

    std::cout << issuance_a.ToJson() << std::endl;
    std::cout << issuance_b.ToJson() << std::endl;

    ASSERT_FALSE(error);
    ASSERT_EQ(issuance_a, issuance_b);

    // Issue Additional
    //
    //
    auto adtl_a(GenerateIssueAdtl());

    error = false;
    IssueAdditional adtl_b(error,
                           adtl_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(adtl_a, adtl_b);

    // Change Setting
    //
    //
    auto change_a(GenerateChangeSetting());

    error = false;
    ChangeSetting change_b(error,
                           change_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(change_a, change_b);

    // Immute Setting
    //
    //
    auto immute_a(GenerateImmuteSetting());

    error = false;
    ImmuteSetting immute_b(error,
                           immute_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(immute_a, immute_b);

    // Revoke
    //
    //
    auto revoke_a(GenerateRevoke());

    error = false;
    Revoke revoke_b(error,
                    revoke_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(revoke_a, revoke_b);

    // Adjust User Status
    //
    //
    auto adjust_status_a(GenerateAdjustUserStatus());

    error = false;
    AdjustUserStatus adjust_status_b(error,
                                     adjust_status_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(adjust_status_a, adjust_status_b);

    // Adjust Fee
    //
    //
    auto adjust_fee_a(GenerateAdjustFee());

    error = false;
    AdjustFee adjust_fee_b(error,
                           adjust_fee_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(adjust_fee_a, adjust_fee_b);

    // Update Issuer Info
    //
    //
    auto info_a(GenerateIssuerInfo());

    error = false;
    UpdateIssuerInfo info_b(error,
                            info_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(info_a, info_b);

    // Update Controller
    //
    //
    auto controller_a(GenerateUpdateController());

    error = false;
    UpdateController controller_b(error,
                                  controller_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(controller_a, controller_b);

    // Burn
    //
    //
    auto burn_a(GenerateBurn());

    error = false;
    Burn burn_b(error,
                burn_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(burn_a, burn_b);

    // Distribute
    //
    //
    auto distribute_a(GenerateDistribute());

    error = false;
    Distribute distribute_b(error,
                            distribute_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(distribute_a, distribute_b);

    // Withdraw Fee
    //
    //
    auto withdraw_a(GenerateWithdrawFee());

    error = false;
    WithdrawFee withdraw_b(error,
                           withdraw_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(withdraw_a, withdraw_b);

    // Token Send
    //
    //
    auto send_a(GenerateTokenSend());

    error = false;
    TokenSend send_b(error,
                     send_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(send_a, send_b);
}

#endif // #ifdef Unit_Test_Request_Serialization