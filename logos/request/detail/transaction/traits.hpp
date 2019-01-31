#pragma once

#include <logos/lib/numbers.hpp>

template<typename, typename>
struct TransactionTraits
{};

template<>
struct TransactionTraits<std::string, uint16_t>
{
    struct Decode
    {
        uint16_t operator()(const std::string & data) const
        {
            return std::stoul(data);
        }
    };

    struct Encode
    {
        std::string operator()(uint16_t data) const
        {
            return std::to_string(data);
        }
    };
};

template<>
struct TransactionTraits<std::string, logos::amount>
{
    struct Decode
    {
        logos::amount operator() (const std::string & data) const
        {
            logos::amount amount;
            amount.decode_dec(data);

            return amount;
        }
    };

    struct Encode
    {
        std::string operator()(const logos::amount & data) const
        {
            return data.to_string_dec();
        }
    };
};
