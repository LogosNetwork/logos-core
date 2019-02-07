#include <logos/request/fields.hpp>

template<typename AmountType>
Transaction<AmountType>::Transaction(const Address & destination,
                                     const AmountType & amount)
    : destination(destination)
    , amount(amount)
{}

template<typename AmountType>
Transaction<AmountType>::Transaction(bool & error,
                                     std::basic_streambuf<uint8_t> & stream)
{
    error = logos::read(stream, destination);
    if(error)
    {
        return;
    }

    error = logos::read(stream, amount);
}

template<typename AmountType>
Transaction<AmountType>::Transaction(bool & error,
                                     boost::property_tree::ptree const & tree)
{
    using namespace request::fields;

    try
    {
        error = destination.decode_account(tree.get<std::string>(DESTINATION));
        if(error)
        {
            return;
        }

        amount = GetAmount()(tree.get<std::string>(AMOUNT));
    }
    catch(...)
    {
        error = true;
    }
}

template<typename AmountType>
boost::property_tree::ptree Transaction<AmountType>::SerializeJson() const
{
    using namespace request::fields;

    boost::property_tree::ptree tree;

    tree.put(DESTINATION, destination.to_account());
    tree.put(AMOUNT, PutAmount()(amount));

    return tree;
}

template<typename AmountType>
uint64_t Transaction<AmountType>::Serialize(logos::stream & stream) const
{
    return logos::write(stream, destination) +
           logos::write(stream, amount);
}

template<typename AmountType>
void Transaction<AmountType>::Hash(blake2b_state & hash) const
{
    destination.Hash(hash);
    blake2b_update(&hash, &amount, sizeof(amount));
}

template<typename AmountType>
uint16_t Transaction<AmountType>::WireSize()
{
    return sizeof(destination.bytes) +
           sizeof(amount);
}

template<typename AmountType>
bool Transaction<AmountType>::operator== (const Transaction<AmountType> & other) const
{
    return destination == other.destination &&
           amount == other.amount;
}
