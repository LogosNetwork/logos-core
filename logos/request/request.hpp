#pragma once

#include <logos/node/utility.hpp>
#include <logos/lib/utility.hpp>
#include <logos/lib/numbers.hpp>
#include <logos/common.hpp>

#include <boost/property_tree/ptree.hpp>

enum class RequestType : uint8_t
{
    // Native Logos Requests
    //
    Send                = 0,
    ChangeRep           = 1,

    // Administrative Token
    // Requests
    //
    IssueTokens        = 2,
    ChangeTokenSetting = 3,
    IssueAdtlTokens    = 4,
    ImmuteTokenSetting = 5,
    RevokeTokens       = 6,
    FreezeTokens       = 7,
    SetTokenFee        = 8,
    UpdateWhitelist    = 9,
    UpdateIssuerInfo   = 10,
    UpdateController   = 11,
    BurnTokens         = 12,
    DistributeTokens   = 13,
    WithdrawTokens     = 14,

    // Token User Requests
    //
    SendTokens         = 15,

    // Unknown
    //
    Unknown            = 16
};

class ReservationsProvider;

struct Request
{
    using BlockHash      = logos::block_hash;
    using AccountAddress = logos::uint256_union;

    Request() = default;

    Request(RequestType type,
            const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountPrivKey & priv,
            const AccountPubKey & pub);

    Request(RequestType type,
            const AccountAddress & origin,
            const BlockHash & previous,
            const Amount & fee,
            uint32_t sequence,
            const AccountSig & signature);

    Request(bool & error,
            std::basic_streambuf<uint8_t> & stream);

    Request(bool & error,
            boost::property_tree::ptree const & tree);

    virtual ~Request() = default;

    virtual logos::AccountType GetAccountType() const;

    // The account that will own the
    // request.
    //
    virtual AccountAddress GetAccount() const;

    // The account from which an amount
    // is being deducted.
    //
    virtual AccountAddress GetSource() const;

    virtual Amount GetLogosTotal() const;
    virtual uint16_t GetTokenTotal() const;

    void Sign(AccountPrivKey const & priv, AccountPubKey const & pub);
    bool VerifySignature(AccountPubKey const & pub) const;

    std::string ToJson() const;

    virtual bool Validate(logos::process_return & result,
                          std::shared_ptr<logos::Account> info) const;
    virtual bool Validate(logos::process_return & result) const;

    virtual boost::property_tree::ptree SerializeJson() const;
    virtual uint64_t Serialize(logos::stream & stream) const;
    logos::mdb_val SerializeDB(std::vector<uint8_t> & buf) const;

    virtual BlockHash GetHash () const;

    BlockHash Hash() const;
    virtual void Hash(blake2b_state & hash) const;

    virtual uint16_t WireSize() const;

    RequestType       type = RequestType::Unknown;
    AccountAddress    origin;
    AccountSig        signature;
    BlockHash         previous;
    BlockHash         next;
    Amount            fee;
    uint32_t          sequence = 0;
    mutable BlockHash digest;
};
