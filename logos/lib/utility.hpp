#pragma once

#include <boost/property_tree/ptree.hpp>
#include <logos/lib/numbers.hpp>

#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <bitset>

namespace logos
{

// We operate on streams of uint8_t by convention
using stream = std::basic_streambuf<uint8_t>;

// Read a raw byte stream the size of `T' and fill value.
template <typename T>
bool read (logos::stream & stream_a, T & value)
{
    static_assert (std::is_pod<T>::value, "Can't stream read non-standard layout types");
    auto amount_read (stream_a.sgetn (reinterpret_cast<uint8_t *> (&value), sizeof (value)));
    return amount_read != sizeof (value);
}

template <typename T>
uint64_t write (logos::stream & stream_a, T const & value)
{
    static_assert (std::is_pod<T>::value, "Can't stream write non-standard layout types");
    auto amount_written (stream_a.sputn (reinterpret_cast<uint8_t const *> (&value), sizeof (value)));
    assert (amount_written == sizeof (value));
    return amount_written;
}

template<typename T = uint8_t>
bool read (logos::stream & stream_a, std::string & value)
{
    static_assert(std::is_integral<T>::value,
                  "Integral type required.");

    T len;
    if(read(stream_a, len))
    {
        return true;
    }

    value.resize(len);
    uint64_t pos = 0;

    while(pos < len)
    {
        auto next = stream_a.sbumpc();
        if(next == stream::traits_type::eof())
        {
            return true;
        }

        value[pos++] = stream::traits_type::to_char_type(next);
    }

    return false;
}

template<typename T = uint8_t>
uint64_t write (logos::stream & stream_a, const std::string & value)
{
    static_assert(std::is_integral<T>::value,
                  "Integral type required.");

    T len = value.size();

    uint64_t written;
    if((written = write(stream_a, len)) != sizeof(T))
    {
        return written;
    }

    uint64_t pos = 0;

    while(pos < value.size())
    {
        using C = stream::traits_type::char_type;
        if(stream_a.sputc(C(value[pos++])) == stream::traits_type::eof())
        {
            return pos - 1 + sizeof(T);
        }
    }

    return value.size() + sizeof(T);
}

template<size_t N>
bool read (logos::stream & stream_a, std::bitset<N> & value)
{
    uint64_t val;
    if(read(stream_a, val))
    {
        return true;
    }

    value = std::bitset<N>(val);

    return false;
}

template<size_t N>
uint64_t write (logos::stream & stream_a, const std::bitset<N> & value)
{
    uint64_t val;

    try
    {
        val = value.to_ullong();
    }
    catch(...)
    {
        return 0;
    }

    return write(stream_a, val);
}

template <typename T>
bool peek (logos::stream & stream_a, T & value)
{
    static_assert (std::is_pod<T>::value, "Can't stream read non-standard layout types");
    auto amount_read (stream_a.sgetn (reinterpret_cast<uint8_t *> (&value), sizeof (value)));
    bool failure = amount_read != sizeof (value);
    if(!failure)
    {
        constexpr int failed_offset = -1;

        failure = (stream_a.pubseekoff(-stream::off_type(amount_read),
                                       std::ios_base::seekdir::_S_cur,
                                       std::ios_base::in)
                   == stream::pos_type(stream::off_type(failed_offset)));
    }
    return failure;
}

bool read (logos::stream & stream_a, uint128_union & value);
uint64_t write (logos::stream & stream_a, uint128_union const & value);
bool read (logos::stream & stream_a, uint256_union & value);
uint64_t write (logos::stream & stream_a, uint256_union const & value);
bool read (logos::stream & stream_a, uint512_union & value);
uint64_t write (logos::stream & stream_a, uint512_union const & value);
bool read (logos::stream & stream_a, std::vector<bool> & value);
uint64_t write (logos::stream & stream_a, const std::vector<bool> & value);

// Lower priority of calling work generating thread
void work_thread_reprioritize ();
template <typename... T>
class observer_set
{
public:
    void add (std::function<void(T...)> const & observer_a)
    {
        std::lock_guard<std::mutex> lock (mutex);
        observers.push_back (observer_a);
    }
    void operator() (T... args)
    {
        std::lock_guard<std::mutex> lock (mutex);
        for (auto & i : observers)
        {
            i (args...);
        }
    }
    std::mutex mutex;
    std::vector<std::function<void(T...)>> observers;
};

std::string string_to_hex_str(const std::string& input);

} // namespace logos

template<size_t N>
struct BitField
{
    using GetField = std::function<std::string (size_t i)>;
    using GetPos   = std::function<size_t (bool &, const std::string &)>;

    BitField(const std::string & field)
        : field(field)
    {}

    BitField(const std::bitset<N> & field)
        : field(field)
    {}

    BitField(bool & error,
             std::basic_streambuf<uint8_t> & stream)
    {
        error = Deserialize(stream);
    }

    BitField() = default;

    void Set(size_t pos, bool value = true)
    {
        field.set(pos, value);
    }

    bool operator[] (size_t pos) const
    {
        return field[pos];
    }

    BitField & operator= (const std::bitset<N> & field)
    {
        this->field = field;
        return *this;
    }

    BitField & operator= (const std::string & field)
    {
        this->field = std::bitset<N>(field);
        return *this;
    }

    bool operator== (const BitField & other) const
    {
        return field == other.field;
    }

    void DeserializeJson(bool & error,
                         boost::property_tree::ptree const & tree,
                         GetPos f)
    {
        for(const auto & entry : tree)
        {
            size_t pos(f(error, entry.second.get<std::string>("")));
            if(error)
            {
                return;
            }

            field.set(pos);
        }
    }

    boost::property_tree::ptree SerializeJson(GetField f) const
    {
        boost::property_tree::ptree tree;

        for(size_t i = 0; i < N; ++i)
        {
            if(field[i])
            {
                boost::property_tree::ptree t;
                t.put("", f(i));

                tree.push_back(std::make_pair("", t));

                // TODO: t.push_back("",
                //                   boost::property_tree::ptree(f(i))); ?
            }
        }

        return tree;
    }

    bool Deserialize(logos::stream & stream)
    {
        return logos::read(stream, field);
    }

    uint64_t Serialize(logos::stream & stream) const
    {
        return logos::write(stream, field);
    }

    void Hash(blake2b_state & hash) const
    {
        auto f(field.to_ullong());
        blake2b_update(&hash, &f, sizeof(f));
    }

    static constexpr uint64_t WireSize()
    {
        using B = std::bitset<N>;

        return sizeof(typename std::result_of<decltype(&B::to_ullong)(B)>::type);
    }

    std::bitset<N> field;
};
