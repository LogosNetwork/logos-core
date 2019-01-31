#pragma once

#include <logos/request/request.hpp>

RequestType GetRequestType(bool &error, std::string data);
std::string GetRequestTypeField(RequestType type);

template<typename T>
uint16_t VectorWireSize(const std::vector<T> & v)
{
    // The size of the vector's
    // elements plus the size
    // of the field denoting
    // the number of elements.
    //
    return (T::WireSize() * v.size()) + sizeof(uint8_t);
}

template<typename T = uint8_t>
T StringWireSize(const std::string & s)
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
uint64_t SerializeVector(logos::stream & stream, const std::vector<T> & v)
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
