#pragma once

#include <logos/lib/numbers.hpp>
#include <logos/lib/utility.hpp>

#include <boost/property_tree/ptree.hpp>

enum class RequestType : uint8_t
{
    // Native Requests
    //
    Send                = 0,
    ChangeRep           = 1,

    // Token Administrative
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

RequestType GetRequestType(bool &error, std::string data);
std::string GetRequestTypeField(RequestType type);

struct Request
{
    using BlockHash      = logos::block_hash;
    using AccountAddress = logos::uint256_union;

    Request() = default;

    Request(bool & error,
            std::basic_streambuf<uint8_t> & stream);

    Request(bool & error,
            boost::property_tree::ptree const & tree);

    virtual ~Request() {}

    std::string ToJson() const;
    virtual boost::property_tree::ptree SerializeJson() const;
    virtual uint64_t Serialize(logos::stream & stream) const;

    BlockHash Hash() const;
    virtual void Hash(blake2b_state & hash) const = 0;

    virtual uint16_t WireSize() const;

    template<typename T>
    uint16_t VectorWireSize(const std::vector<T> & v) const
    {
        // The size of the vector's
        // elements plus the size
        // of the field denoting
        // the number of elements.
        //
        return (T::WireSize() * v.size()) + sizeof(uint8_t);
    }

    template<typename T = uint8_t>
    T StringWireSize(const std::string & s) const
    {
        static_assert(std::is_integral<T>::value,
                      "Integral type required.");

        assert(s.size() <= std::numeric_limits<T>::max());

        // Length of string plus one
        // byte to denote the length.
        //
        return s.size() + sizeof(T);
    }

    template<typename T, typename S = uint8_t>
    uint64_t SerializeVector(logos::stream & stream, const std::vector<T> & v) const
    {
        static_assert(std::is_integral<S>::value,
                      "Integral type required.");

        assert(v.size() < std::numeric_limits<S>::max());

        uint64_t written = logos::write(stream, S(v.size()));

        for(size_t i = 0; i < v.size(); ++i)
        {
            written += v[i].Serialize(stream);
        }

        return written;
    }

    RequestType type;
    BlockHash   previous;
    BlockHash   next;
};
