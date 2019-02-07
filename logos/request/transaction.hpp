#pragma once

#include <logos/request/detail/transaction/traits.hpp>
#include <logos/consensus/messages/byte_arrays.hpp>
#include <logos/lib/utility.hpp>

#include <boost/property_tree/ptree.hpp>

// TODO: Traits::size, Traits::hash
//
template<typename AmountType>
struct Transaction
{
    using Address   = AccountAddress;
    using Traits    = TransactionTraits<std::string, AmountType>;
    using GetAmount = typename Traits::Decode;
    using PutAmount = typename Traits::Encode;

    Transaction() = default;

    Transaction(const Address & destination,
                const AmountType & amount);

    Transaction(bool & error,
                std::basic_streambuf<uint8_t> & stream);

    Transaction(bool & error,
                boost::property_tree::ptree const & tree);

    boost::property_tree::ptree SerializeJson() const;
    uint64_t Serialize(logos::stream & stream) const;

    void Hash(blake2b_state & hash) const;

    static uint16_t WireSize();

    Address    destination;
    AmountType amount;
};

#include <logos/request/transaction.ipp>
