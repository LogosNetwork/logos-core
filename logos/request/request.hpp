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
    IssueAdtlTokens    = 3,
    ImmuteTokenSetting = 4,
    RevokeTokens       = 5,
    FreezeTokens       = 6,
    SetTokenFee        = 7,
    UpdateWhitelist    = 8,
    UpdateIssuerInfo   = 9,
    UpdateController   = 10,
    BurnTokens         = 11,
    DistributeTokens   = 12,
    WithdrawTokens     = 13,

    // Token User Requests
    //
    SendTokens         = 14,

    // Unknown
    //
    Unknown            = 15
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
            const AccountPrivKey & priv,
            const AccountPubKey & pub);

    Request(RequestType type,
            const AccountAddress & origin,
            const BlockHash & previous,
            const AccountSig & signature);

    Request(bool & error,
            std::basic_streambuf<uint8_t> & stream);

    Request(bool & error,
            boost::property_tree::ptree const & tree);

    virtual ~Request() = default;

    void Sign(AccountPrivKey const & priv, AccountPubKey const & pub);
    bool VerifySignature(AccountPubKey const & pub) const;

    std::string ToJson() const;

    bool Validate(std::shared_ptr<ReservationsProvider>,
                  logos::process_return & result,
                  bool allow_duplicates) const;

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
    mutable BlockHash digest;
};
