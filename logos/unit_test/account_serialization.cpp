#include <gtest/gtest.h>

#include <logos/token/account.hpp>

#define Unit_Test_Token_Account_Serialization

#ifdef Unit_Test_Token_Account_Serialization

auto DoGetStreamedData = [](const auto & data, auto & buf)
{
    buf.clear();
    {
        logos::vectorstream stream(buf);
        data.Serialize(stream);
    }
};

auto GetStreamedData = [](const auto & data)
{
    std::vector<uint8_t> buf;
    DoGetStreamedData(data, buf);

    return buf;
};

TEST (Token_Account_Serialization, stream_methods)
{
    TokenAccount account_a;

    account_a.token_balance = 5000;
    account_a.token_fee_balance = 50;
    account_a.fee_type = TokenFeeType::Flat;
    account_a.fee_rate = 1;
    account_a.symbol = "MYC";
    account_a.name = "MyCoin";
    account_a.issuer_info = "MyCoin was created by Bob.";

    ControllerInfo controller;
    controller.account.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    controller.privileges = "11111111110000000000";

    account_a.controllers = {controller};
    account_a.settings = "1111100000";

    auto buf(GetStreamedData(account_a));

    TokenAccount account_b;

    logos::bufferstream stream(buf.data(), buf.size());
    auto error = account_b.Deserialize(stream);

    ASSERT_FALSE(error);
    ASSERT_EQ(account_a.token_balance, account_b.token_balance);
    ASSERT_EQ(account_a.token_fee_balance, account_b.token_fee_balance);
    ASSERT_EQ(account_a.fee_type, account_b.fee_type);
    ASSERT_EQ(account_a.fee_rate, account_b.fee_rate);
    ASSERT_EQ(account_a.symbol, account_b.symbol);
    ASSERT_EQ(account_a.name, account_b.name);
    ASSERT_EQ(account_a.issuer_info, account_b.issuer_info);
    ASSERT_EQ(account_a.controllers, account_b.controllers);
    ASSERT_EQ(account_a.settings, account_b.settings);
}

TEST (Token_Account_Serialization, database_methods)
{
    TokenAccount account_a;

    account_a.token_balance = 5000;
    account_a.token_fee_balance = 50;
    account_a.fee_type = TokenFeeType::Flat;
    account_a.fee_rate = 1;
    account_a.symbol = "MYC";
    account_a.name = "MyCoin";
    account_a.issuer_info = "MyCoin was created by Bob.";

    ControllerInfo controller;
    controller.account.decode_account("lgs_38qxo4xfj1ic9c5iyi867x5a8do7yfqkywyxbxtm4wk3ssdgarbxhejd6jju");
    controller.privileges = "11111111110000000000";

    account_a.controllers = {controller};
    account_a.settings = "1111100000";

    std::vector<uint8_t> buf;

    auto error = false;
    TokenAccount account_b(error,
                           account_a.to_mdb_val(buf));

    ASSERT_FALSE(error);
    ASSERT_EQ(account_a.token_balance, account_b.token_balance);
    ASSERT_EQ(account_a.token_fee_balance, account_b.token_fee_balance);
    ASSERT_EQ(account_a.fee_type, account_b.fee_type);
    ASSERT_EQ(account_a.fee_rate, account_b.fee_rate);
    ASSERT_EQ(account_a.symbol, account_b.symbol);
    ASSERT_EQ(account_a.name, account_b.name);
    ASSERT_EQ(account_a.issuer_info, account_b.issuer_info);
    ASSERT_EQ(account_a.controllers, account_b.controllers);
    ASSERT_EQ(account_a.settings, account_b.settings);
}

#endif // #ifdef Unit_Test_Token_Account_Serialization
