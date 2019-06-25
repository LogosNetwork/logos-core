#include <gtest/gtest.h>

#include <boost/multiprecision/cpp_dec_float.hpp>
#include <logos/request/utility.hpp>
#include <logos/token/requests.hpp>
#include <logos/rewards/claim.hpp>

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
        "setting": "freeze"
     })%%%";

    tree = get_tree(immute_setting_json);
    ImmuteSetting immute_setting(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(immute_setting.type, RequestType::ImmuteSetting);
    ASSERT_EQ(immute_setting.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(immute_setting.fee.number(), 100);
    ASSERT_EQ(immute_setting.sequence, 1);
    ASSERT_EQ(immute_setting.setting, TokenSetting::Freeze);

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

    // Withdraw Fee
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

    // Withdraw Logos
    //
    //
    char const * withdraw_logos_json = R"%%%({
        "type": "withdraw_logos",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "600",
        "sequence": "1",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "token_id": "0000000000000000000000000000000000000000000000000000000000000000",
        "transaction" : {
            "destination": "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz",
            "amount": "1000"
        }
     })%%%";

    tree = get_tree(withdraw_logos_json);
    WithdrawFee withdraw_logos(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(withdraw_logos.type, RequestType::WithdrawLogos);
    ASSERT_EQ(withdraw_logos.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(withdraw_logos.fee.number(), 600);
    ASSERT_EQ(withdraw_logos.sequence, 1);
    ASSERT_EQ(withdraw_logos.transaction.destination.to_account(),
              "lgs_3niwauda6c9nhf4dt8hxowgp5gsembnqqiukm8bh3ikrwm6z1uwjctrsi9tz");
    ASSERT_EQ(withdraw_logos.transaction.amount, 1000);

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

    // Proxy
    //
    //
    char const * proxy_json = R"%%%({
        "type": "proxy",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000",
        "sequence": "10",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "epoch_num": "5",
        "governance_subchain_previous": "E9D4A8BC6F03EA28F097D8DA7DFF085D3E2812EC31786AD800B8468F1CBAADA4",
        "lock_proxy": "454545",
        "representative": "lgs_1sibjaeaceh59dh7fefo49narpsoytqac5hafhujum3grnd7qrhbczfy9wx8",
        "work": "6"
     })%%%";

    tree = get_tree(proxy_json);
    Proxy proxy(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(proxy.type, RequestType::Proxy);
    ASSERT_EQ(proxy.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(proxy.fee.number(), 10000);
    ASSERT_EQ(proxy.sequence, 10);
    ASSERT_EQ(proxy.next.to_string(), "0000000000000000000000000000000000000000000000000000000000000000");
    ASSERT_EQ(proxy.epoch_num, 5);
    ASSERT_EQ(proxy.governance_subchain_prev.to_string(),
              "E9D4A8BC6F03EA28F097D8DA7DFF085D3E2812EC31786AD800B8468F1CBAADA4");
    ASSERT_EQ(proxy.lock_proxy.number(), 454545);
    ASSERT_EQ(proxy.rep.to_account(), "lgs_1sibjaeaceh59dh7fefo49narpsoytqac5hafhujum3grnd7qrhbczfy9wx8");
    ASSERT_EQ(proxy.work, 6);

    // Stake
    //
    //
    char const * stake_json = R"%%%({
        "type": "stake",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000",
        "sequence": "99",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "epoch_num": "222",
        "governance_subchain_previous": "E9D4A8BC6F03EA28F097D8DA7DFF085D3E2812EC31786AD800B8468F1CBAADA4",
        "stake": "111111",
        "work": "6"
     })%%%";

    tree = get_tree(stake_json);
    Stake stake(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(stake.type, RequestType::Stake);
    ASSERT_EQ(stake.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(stake.fee.number(), 10000);
    ASSERT_EQ(stake.sequence, 99);
    ASSERT_EQ(stake.next.to_string(), "0000000000000000000000000000000000000000000000000000000000000000");
    ASSERT_EQ(stake.epoch_num, 222);
    ASSERT_EQ(stake.governance_subchain_prev.to_string(),
              "E9D4A8BC6F03EA28F097D8DA7DFF085D3E2812EC31786AD800B8468F1CBAADA4");
    ASSERT_EQ(stake.stake.number(), 111111);
    ASSERT_EQ(stake.work, 6);

    // Unstake
    //
    //
    char const * unstake_json = R"%%%({
        "type": "unstake",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000",
        "sequence": "100",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "epoch_num": "222",
        "governance_subchain_previous": "D07FA4A78CFDAE9E86C746F4A42449FEA564E86D44D41AFC133A14080E8735E9",
        "work": "100"
     })%%%";

    tree = get_tree(unstake_json);
    Unstake unstake(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(unstake.type, RequestType::Unstake);
    ASSERT_EQ(unstake.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(unstake.fee.number(), 10000);
    ASSERT_EQ(unstake.sequence, 100);
    ASSERT_EQ(unstake.next.to_string(), "0000000000000000000000000000000000000000000000000000000000000000");
    ASSERT_EQ(unstake.epoch_num, 222);
    ASSERT_EQ(unstake.governance_subchain_prev.to_string(),
              "D07FA4A78CFDAE9E86C746F4A42449FEA564E86D44D41AFC133A14080E8735E9");
    ASSERT_EQ(unstake.work, 0x100);

    // ElectionVote
    //
    //
    char const * vote_json = R"%%%({
        "type": "election_vote",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000",
        "sequence": "100",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "epoch_num": "222",
        "governance_subchain_previous": "D07FA4A78CFDAE9E86C746F4A42449FEA564E86D44D41AFC133A14080E8735E9",
        "votes": [
            {
                 "account" : "lgs_1sibjaeaceh59dh7fefo49narpsoytqac5hafhujum3grnd7qrhbczfy9wx8",
                 "num_votes" : "5"
            },
            {
                 "account" : "lgs_15p6h3z7dgif1kt8skmdmo8xmobh3xyfzthoden6jqu34t6i4sgtcr4pfj5h",
                 "num_votes" : "2"
            },
            {
                 "account" : "lgs_1mkqajo9pedc1x764b5y5yzkykcm3h3hx1bumznzhgjqimjpajy9w5qfsis6",
                 "num_votes" : "1"
            }
        ],
        "work": "100"
     })%%%";

    tree = get_tree(vote_json);
    ElectionVote vote(error, tree);

    using CandidateVotePair = ElectionVote::CandidateVotePair;

    ASSERT_FALSE(error);
    ASSERT_EQ(vote.type, RequestType::ElectionVote);
    ASSERT_EQ(vote.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(vote.fee.number(), 10000);
    ASSERT_EQ(vote.sequence, 100);
    ASSERT_EQ(vote.next.to_string(), "0000000000000000000000000000000000000000000000000000000000000000");
    ASSERT_EQ(vote.epoch_num, 222);
    ASSERT_EQ(vote.governance_subchain_prev.to_string(),
              "D07FA4A78CFDAE9E86C746F4A42449FEA564E86D44D41AFC133A14080E8735E9");
    ASSERT_EQ(vote.votes[0],
              CandidateVotePair("lgs_1sibjaeaceh59dh7fefo49narpsoytqac5hafhujum3grnd7qrhbczfy9wx8", 5));
    ASSERT_EQ(vote.votes[1],
              CandidateVotePair("lgs_15p6h3z7dgif1kt8skmdmo8xmobh3xyfzthoden6jqu34t6i4sgtcr4pfj5h", 2));
    ASSERT_EQ(vote.votes[2],
              CandidateVotePair("lgs_1mkqajo9pedc1x764b5y5yzkykcm3h3hx1bumznzhgjqimjpajy9w5qfsis6", 1));
    ASSERT_EQ(vote.work, 0x100);

    // AnnounceCandidacy
    //
    //
    char const * announce_json = R"%%%({
        "type": "announce_candidacy",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000",
        "sequence": "100",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "epoch_num": "222",
        "governance_subchain_previous": "D07FA4A78CFDAE9E86C746F4A42449FEA564E86D44D41AFC133A14080E8735E9",
        "set_stake": "true",
        "stake": "100009",
        "bls_key": "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
        "ecies_key": "3059301306072a8648ce3d020106082a8648ce3d030107034200048e1ad798008baac3663c0c1a6ce04c7cb632eb504562de923845fccf39d1c46dee52df70f6cf46f1351ce7ac8e92055e5f168f5aff24bcaab7513d447fd677d3",
        "levy_percentage": "4",
        "work": "100"
     })%%%";

    tree = get_tree(announce_json);
    AnnounceCandidacy announce(error, tree);

    std::string ecies = "3059301306072a8648ce3d020106082a8648ce3d030107034200048e1ad798008baac3663c0c"
                        "1a6ce04c7cb632eb504562de923845fccf39d1c46dee52df70f6cf46f1351ce7ac8e92055e5f"
                        "168f5aff24bcaab7513d447fd677d3";

    ASSERT_FALSE(error);
    ASSERT_EQ(announce.type, RequestType::AnnounceCandidacy);
    ASSERT_EQ(announce.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(announce.fee.number(), 10000);
    ASSERT_EQ(announce.sequence, 100);
    ASSERT_EQ(announce.next.to_string(), "0000000000000000000000000000000000000000000000000000000000000000");
    ASSERT_EQ(announce.epoch_num, 222);
    ASSERT_EQ(announce.set_stake, true);
    ASSERT_EQ(announce.stake.number(), 100009);
    ASSERT_EQ(announce.bls_key, DelegatePubKey(std::string(128, '0')));
    ASSERT_EQ(announce.ecies_key, ECIESPublicKey(ecies, true));
    ASSERT_EQ(announce.levy_percentage, 4);
    ASSERT_EQ(announce.work, 0x100);

    // RenounceCandidacy
    //
    //
    char const * renounce_json = R"%%%({
        "type": "renounce_candidacy",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "50000",
        "sequence": "10000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "epoch_num": "222",
        "governance_subchain_previous": "D07FA4A78CFDAE9E86C746F4A42449FEA564E86D44D41AFC133A14080E8735E9",
        "set_stake": "false",
        "stake": "100009",
        "work": "100"
     })%%%";

    tree = get_tree(renounce_json);
    RenounceCandidacy renounce(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(renounce.type, RequestType::RenounceCandidacy);
    ASSERT_EQ(renounce.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(renounce.fee.number(), 50000);
    ASSERT_EQ(renounce.sequence, 10000);
    ASSERT_EQ(renounce.next.to_string(), "0000000000000000000000000000000000000000000000000000000000000000");
    ASSERT_EQ(renounce.epoch_num, 222);
    ASSERT_EQ(renounce.set_stake, false);
    ASSERT_EQ(renounce.stake.number(), 100009);
    ASSERT_EQ(renounce.work, 0x100);

    // StartRepresenting
    //
    //
    char const * start_json = R"%%%({
        "type": "start_representing",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "50000",
        "sequence": "10000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "epoch_num": "9001",
        "governance_subchain_previous": "D07FA4A78CFDAE9E86C746F4A42449FEA564E86D44D41AFC133A14080E8735E9",
        "set_stake": "true",
        "stake": "20",
        "levy_percentage": "90",
        "work": "50"
     })%%%";

    tree = get_tree(start_json);
    StartRepresenting start(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(start.type, RequestType::StartRepresenting);
    ASSERT_EQ(start.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(start.fee.number(), 50000);
    ASSERT_EQ(start.sequence, 10000);
    ASSERT_EQ(start.next.to_string(), "0000000000000000000000000000000000000000000000000000000000000000");
    ASSERT_EQ(start.epoch_num, 9001);
    ASSERT_EQ(start.set_stake, true);
    ASSERT_EQ(start.stake.number(), 20);
    ASSERT_EQ(start.levy_percentage, 90);
    ASSERT_EQ(start.work, 0x50);

    // StopRepresenting
    //
    //
    char const * stop_json = R"%%%({
        "type": "stop_representing",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "50000",
        "sequence": "10000",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "epoch_num": "9001",
        "governance_subchain_previous": "D07FA4A78CFDAE9E86C746F4A42449FEA564E86D44D41AFC133A14080E8735E9",
        "stake": "20000",
        "work": "22222"
     })%%%";

    tree = get_tree(stop_json);
    StopRepresenting stop(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(stop.type, RequestType::StopRepresenting);
    ASSERT_EQ(stop.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(stop.fee.number(), 50000);
    ASSERT_EQ(stop.sequence, 10000);
    ASSERT_EQ(stop.next.to_string(), "0000000000000000000000000000000000000000000000000000000000000000");
    ASSERT_EQ(stop.epoch_num, 9001);
    ASSERT_EQ(stop.set_stake, true);
    ASSERT_EQ(stop.stake.number(), 20000);
    ASSERT_EQ(stop.work, 0x22222);

    // Claim
    //
    //
    char const * claim_json = R"%%%({
        "type": "claim",
        "origin": "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio",
        "signature": "0000000000000000000000000000000000000000000000000000000000000000",
        "previous": "0000000000000000000000000000000000000000000000000000000000000000",
        "fee": "10000",
        "sequence": "5",
        "next": "0000000000000000000000000000000000000000000000000000000000000000",
        "epoch_hash": "0000000000000000000000000000000000000000000000000000000000000000",
        "epoch_number": "23",
        "work": "6"
     })%%%";

    tree = get_tree(claim_json);
    Claim claim(error, tree);

    ASSERT_FALSE(error);
    ASSERT_EQ(claim.type, RequestType::Claim);
    ASSERT_EQ(claim.origin.to_account(), "lgs_3njdeqz6nywhb4so3w85sndaojguptiw43w4wi3nfunrd8yesmif96nwtxio");
    ASSERT_EQ(claim.fee.number(), 10000);
    ASSERT_EQ(claim.sequence, 5);
    ASSERT_EQ(claim.next.to_string(), "0000000000000000000000000000000000000000000000000000000000000000");
    ASSERT_EQ(claim.epoch_hash.to_string(), "0000000000000000000000000000000000000000000000000000000000000000");
    ASSERT_EQ(claim.epoch_number, 23);
    ASSERT_EQ(claim.work, 6);
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
    AccountAddress controller1;
    controller1.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    AccountAddress controller2;
    controller2.decode_account("lgs_15p6h3z7dgif1kt8skmdmo8xmobh3xyfzthoden6jqu34t6i4sgtcr4pfj5h");
    issuance.controllers =
        {
            {
                {controller1},
                {"11111111110000000000"}
            },
            {
                {controller2},
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

auto GenerateWithdrawLogos = []()
{
    WithdrawLogos withdraw;

    withdraw.type = RequestType::WithdrawLogos;
    withdraw.transaction.destination.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    withdraw.transaction.amount = 750;

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

auto GenerateProxy = []()
{
    Proxy proxy;

    proxy.type = RequestType::Proxy;

    proxy.epoch_num = 100;
    proxy.governance_subchain_prev = {"CF10488A1FC2ACF845ED3D98F71DF6A4F61AD7543D4F77954C160A28952560F4"};
    proxy.lock_proxy = {9001};
    proxy.rep.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");

    return proxy;
};

auto GenerateStake = []()
{
    Stake stake;

    stake.type = RequestType::Stake;

    stake.epoch_num = 100;
    stake.governance_subchain_prev = {"CF10488A1FC2ACF845ED3D98F71DF6A4F61AD7543D4F77954C160A28952560F4"};
    stake.stake = {5000};

    return stake;
};

auto GenerateUnstake = []()
{
    Unstake unstake;

    unstake.type = RequestType::Unstake;

    unstake.epoch_num = 100;
    unstake.governance_subchain_prev = {"CF10488A1FC2ACF845ED3D98F71DF6A4F61AD7543D4F77954C160A28952560F4"};

    return unstake;
};

auto GenerateElectionVote = []()
{
    ElectionVote vote;

    vote.type = RequestType::ElectionVote;

    vote.epoch_num = 100;
    vote.governance_subchain_prev = {"CF10488A1FC2ACF845ED3D98F71DF6A4F61AD7543D4F77954C160A28952560F4"};
    vote.votes = {
        {"lgs_1sibjaeaceh59dh7fefo49narpsoytqac5hafhujum3grnd7qrhbczfy9wx8", 5},
        {"lgs_1sibjaeaceh59dh7fefo49narpsoytqac5hafhujum3grnd7qrhbczfy9wx8", 2},
        {"lgs_1sibjaeaceh59dh7fefo49narpsoytqac5hafhujum3grnd7qrhbczfy9wx8", 1}
    };

    return vote;
};

auto GenerateAnnounce = []()
{
    AnnounceCandidacy announce;

    std::string ecies = "3059301306072a8648ce3d020106082a8648ce3d030107034200048e1ad798008baac3663c0c"
                        "1a6ce04c7cb632eb504562de923845fccf39d1c46dee52df70f6cf46f1351ce7ac8e92055e5f"
                        "168f5aff24bcaab7513d447fd677d3";

    announce.type = RequestType::AnnounceCandidacy;

    announce.epoch_num = 100;
    announce.governance_subchain_prev = {"CF10488A1FC2ACF845ED3D98F71DF6A4F61AD7543D4F77954C160A28952560F4"};
    announce.set_stake = true;
    announce.stake = {9001};
    announce.levy_percentage = 55;
    announce.bls_key = DelegatePubKey(std::string(128, '0'));
    announce.ecies_key = ECIESPublicKey(ecies, true);

    return announce;
};

auto GenerateRenounce = []()
{
    RenounceCandidacy renounce;

    renounce.type = RequestType::RenounceCandidacy;

    renounce.epoch_num = 100;
    renounce.governance_subchain_prev = {"CF10488A1FC2ACF845ED3D98F71DF6A4F61AD7543D4F77954C160A28952560F4"};
    renounce.set_stake = true;
    renounce.stake = {9001};

    return renounce;
};

auto GenerateStart = []()
{
    StartRepresenting start;

    start.type = RequestType::StartRepresenting;

    start.epoch_num = 100;
    start.governance_subchain_prev = {"CF10488A1FC2ACF845ED3D98F71DF6A4F61AD7543D4F77954C160A28952560F4"};
    start.set_stake = true;
    start.stake = {9001};
    start.levy_percentage = 55;

    return start;
};

auto GenerateStop = []()
{
    StopRepresenting stop;

    stop.type = RequestType::StopRepresenting;

    stop.epoch_num = 100;
    stop.governance_subchain_prev = {"CF10488A1FC2ACF845ED3D98F71DF6A4F61AD7543D4F77954C160A28952560F4"};
    stop.set_stake = true;
    stop.stake = {9001};

    return stop;
};

auto GenerateClaim = []()
{
    Claim claim;

    claim.type = RequestType::Claim;

    claim.epoch_hash = {0xDEADBEEF};
    claim.epoch_number = 23;

    return claim;
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

    // Withdraw Logos
    //
    //
    auto withdraw_logos_a(GenerateWithdrawLogos());
    DoGetStreamedData(withdraw_logos_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    WithdrawLogos withdraw_logos_b(GetRequest<WithdrawLogos>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(withdraw_logos_a, withdraw_logos_b);

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

    // Proxy
    //
    //
    auto proxy_a(GenerateProxy());
    DoGetStreamedData(proxy_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    Proxy proxy_b(GetRequest<Proxy>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(proxy_a, proxy_b);

    // Stake
    //
    //
    auto stake_a(GenerateStake());
    DoGetStreamedData(stake_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    Stake stake_b(GetRequest<Stake>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(stake_a, stake_b);

    // Unstake
    //
    //
    auto unstake_a(GenerateUnstake());
    DoGetStreamedData(unstake_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    Unstake unstake_b(GetRequest<Unstake>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(unstake_a, unstake_b);

    // ElectionVote
    //
    //
    auto vote_a(GenerateElectionVote());
    DoGetStreamedData(vote_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    ElectionVote vote_b(GetRequest<ElectionVote>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(vote_a, vote_b);

    // AnnounceCandidacy
    //
    //
    auto announce_a(GenerateAnnounce());
    DoGetStreamedData(announce_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    AnnounceCandidacy announce_b(GetRequest<AnnounceCandidacy>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(announce_a, announce_b);

    // RenounceCandidacy
    //
    //
    auto renounce_a(GenerateRenounce());
    DoGetStreamedData(renounce_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    RenounceCandidacy renounce_b(GetRequest<RenounceCandidacy>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(renounce_a, renounce_b);

    // StartRepresenting
    //
    //
    auto start_a(GenerateStart());
    DoGetStreamedData(start_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    StartRepresenting start_b(GetRequest<StartRepresenting>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(start_a, start_b);

    // StopRepresenting
    //
    //
    auto stop_a(GenerateStop());
    DoGetStreamedData(stop_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    StopRepresenting stop_b(GetRequest<StopRepresenting>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(stop_a, stop_b);

    // Claim
    //
    //
    auto claim_a(GenerateClaim());
    DoGetStreamedData(claim_a, buf);

    stream.close();
    stream.open(buf.data(), buf.size());

    error = false;
    Claim claim_b(GetRequest<Claim>(error, stream));

    ASSERT_FALSE(error);
    ASSERT_EQ(claim_a, claim_b);
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

    // Withdraw Logos
    //
    //
    auto withdraw_logos_a(GenerateWithdrawLogos());

    buf.clear();

    error = false;
    WithdrawLogos withdraw_logos_b(error,
                                   withdraw_logos_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(withdraw_logos_a, withdraw_logos_b);

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

    // Proxy
    //
    //
    auto proxy_a(GenerateProxy());

    buf.clear();

    error = false;
    Proxy proxy_b(error,
                  proxy_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(proxy_a, proxy_b);

    // Stake
    //
    //
    auto stake_a(GenerateStake());

    buf.clear();

    error = false;
    Stake stake_b(error,
                  stake_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(stake_a, stake_b);

    // Unstake
    //
    //
    auto unstake_a(GenerateUnstake());

    buf.clear();

    error = false;
    Unstake unstake_b(error,
                      unstake_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(unstake_a, unstake_b);

    // ElectionVote
    //
    //
    auto vote_a(GenerateElectionVote());

    buf.clear();

    error = false;
    ElectionVote vote_b(error,
                        vote_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(vote_a, vote_b);

    // AnnounceCandidacy
    //
    //
    auto announce_a(GenerateAnnounce());

    buf.clear();

    error = false;
    AnnounceCandidacy announce_b(error,
                                 announce_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(announce_a, announce_b);

    // RenounceCandidacy
    //
    //
    auto renounce_a(GenerateRenounce());

    buf.clear();

    error = false;
    RenounceCandidacy renounce_b(error,
                                 renounce_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(renounce_a, renounce_b);

    // StartRepresenting
    //
    //
    auto start_a(GenerateStart());

    buf.clear();

    error = false;
    StartRepresenting start_b(error,
                              start_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(start_a, start_b);

    // StopRepresenting
    //
    //
    auto stop_a(GenerateStop());

    buf.clear();

    error = false;
    StopRepresenting stop_b(error,
                            stop_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(stop_a, stop_b);

    // Claim
    //
    //
    auto claim_a(GenerateClaim());

    buf.clear();

    error = false;
    Claim claim_b(error,
                  claim_a.ToDatabase(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(claim_a, claim_b);
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

    // Withdraw Logos
    //
    //
    auto withdraw_logos_a(GenerateWithdrawLogos());

    error = false;
    WithdrawLogos withdraw_logos_b(error,
                                   withdraw_logos_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(withdraw_logos_a, withdraw_logos_b);

    // Token Send
    //
    //
    auto send_a(GenerateTokenSend());

    error = false;
    TokenSend send_b(error,
                     send_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(send_a, send_b);

    // Proxy
    //
    //
    auto proxy_a(GenerateProxy());

    error = false;
    Proxy proxy_b(error,
                  proxy_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(proxy_a, proxy_b);

    // Stake
    //
    //
    auto stake_a(GenerateStake());

    error = false;
    Stake stake_b(error,
                  stake_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(stake_a, stake_b);

    // Unstake
    //
    //
    auto unstake_a(GenerateUnstake());

    error = false;
    Unstake unstake_b(error,
                      unstake_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(unstake_a, unstake_b);

    // ElectionVote
    //
    //
    auto vote_a(GenerateElectionVote());

    error = false;
    ElectionVote vote_b(error,
                        vote_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(vote_a, vote_b);

    // AnnounceCandidacy
    //
    //
    auto announce_a(GenerateAnnounce());

    error = false;
    AnnounceCandidacy announce_b(error,
                                 announce_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(announce_a, announce_b);

    // RenounceCandidacy
    //
    //
    auto renounce_a(GenerateRenounce());

    error = false;
    RenounceCandidacy renounce_b(error,
                                 renounce_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(renounce_a, renounce_b);

    // StartRepresenting
    //
    //
    auto start_a(GenerateStart());

    error = false;
    StartRepresenting start_b(error,
                              start_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(start_a, start_b);

    // StopRepresenting
    //
    //
    auto stop_a(GenerateStop());

    error = false;
    StopRepresenting stop_b(error,
                            stop_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(stop_a, stop_b);

    // Claim
    //
    //
    auto claim_a(GenerateClaim());

    error = false;
    Claim claim_b(error,
                  claim_a.SerializeJson());

    ASSERT_FALSE(error);
    ASSERT_EQ(claim_a, claim_b);
}

#endif // #ifdef Unit_Test_Request_Serialization
