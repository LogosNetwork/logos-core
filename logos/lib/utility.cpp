#include <logos/lib/utility.hpp>

#include <sstream>
#include <iomanip>

std::string logos::unicode_to_hex(std::string const & input)
{
    static const char* const lut = "0123456789abcdef";
    size_t len = input.length();

    std::string output;
    output.reserve(2 * len);
    for (size_t i = 0; i < len; ++i)
    {
        const unsigned char c = input[i];
        output.push_back(lut[c >> 4]);
        output.push_back(lut[c & 15]);
    }
    return output;
}

std::string logos::hex_to_unicode(std::string const & input)
{
    std::string output;

    assert((input.length() % 2) == 0);

    size_t cnt = input.length() / 2;

    output.reserve(cnt);
    for (size_t i = 0; cnt > i; ++i) {
        uint32_t s = 0;
        std::stringstream ss;
        ss << std::hex << input.substr(i * 2, 2);
        ss >> s;

        output.push_back(static_cast<unsigned char>(s));
    }
    return output;
}

bool logos::read (logos::stream & stream_a, Rational & value)
{
    Rational::int_type n;
    Rational::int_type d;

    auto do_read = [&stream_a](auto & val)
    {
        for(auto i = 0; i < (256 / 8); ++i)
        {
            uint8_t byte;
            if(read(stream_a, byte))
            {
                return true;
            }

            Rational::int_type word = byte;
            word <<= 8 * i;
            val |= word;
        }

        return false;
    };

    if(do_read(n))
    {
        return true;
    }

    if(do_read(d))
    {
        return true;
    }

    value.assign(n, d);

    return false;
}

uint64_t logos::write (logos::stream & stream_a, const Rational & value)
{
    uint64_t bytes = 0;

    for(auto val : {value.numerator(), value.denominator()})
    {
        for(auto i = 0; i < (256 / 8); ++i)
        {
            auto byte = static_cast<uint8_t> (val & static_cast<uint8_t> (0xff));
            val >>= 8;

            bytes += write(stream_a, byte);
        }
    }

    return bytes;
}

template<typename U>
static bool ReadUnion(logos::stream & stream_a, U & value)
{
    auto amount_read (stream_a.sgetn (value.bytes.data(), value.bytes.size()));
    return amount_read != value.bytes.size();
}

template<typename U>
static uint64_t WriteUnion (logos::stream & stream_a, U const & value)
{
    auto amount_written (stream_a.sputn (value.bytes.data(), value.bytes.size()));
    assert (amount_written == value.bytes.size());
    return amount_written;
}

bool logos::read (logos::stream & stream_a, uint128_union & value)
{
    return ReadUnion(stream_a, value);
}

uint64_t logos::write (logos::stream & stream_a, uint128_union const & value)
{
    return WriteUnion(stream_a, value);
}

bool logos::read (logos::stream & stream_a, uint256_union & value)
{
    return ReadUnion(stream_a, value);
}

uint64_t logos::write (logos::stream & stream_a, uint256_union const & value)
{
    return WriteUnion(stream_a, value);
}

bool logos::read (logos::stream & stream_a, uint512_union & value)
{
    return ReadUnion(stream_a, value);
}

uint64_t logos::write (logos::stream & stream_a, uint512_union const & value)
{
    return WriteUnion(stream_a, value);
}

uint16_t bits_to_bytes_ceiling(uint16_t x)
{
    return (x + 7) / 8;
}

bool logos::read (logos::stream & stream_a, std::vector<bool> & value)
{
    uint16_t n_bits_le = 0;
    bool error = logos::read(stream_a, n_bits_le);
    if(error)
    {
        return error;
    }
    auto n_bits = le16toh(n_bits_le);
    auto to_read = bits_to_bytes_ceiling(n_bits);

    std::vector<uint8_t> bytes(to_read);
    auto amount_read (stream_a.sgetn (bytes.data(), bytes.size()));
    if(amount_read != to_read)
    {
        return true;
    }

    for( auto b : bytes)
    {
        //std::cout << (int)b << std::endl;
        for(int i = 0; i < 8; ++i)
        {
            if(n_bits-- == 0)
                return false;

            uint8_t mask = 1u<<i;
            if(mask & b)
                value.push_back(true);
            else
                value.push_back(false);
        }
    }

    return false;
}

uint64_t logos::write (logos::stream & stream_a, const std::vector<bool> & value)
{
    uint16_t n_bits = value.size();
    auto n_bits_le = htole16(n_bits);

    auto amount_written (stream_a.sputn ((uint8_t *)&n_bits_le, sizeof(uint16_t)));
    std::vector<uint8_t> buf;
    uint8_t one_byte = 0;
    int cmp = 0;
    for ( auto b : value)
    {
        one_byte = one_byte | ((b ? 1 : 0) << cmp++);
        if(cmp == 8)
        {
            //std::cout << (int)one_byte << std::endl;
            buf.push_back(one_byte);
            cmp = 0;
            one_byte = 0;
        }
    }
    if(cmp != 0)
    {
        //std::cout << (int)one_byte << std::endl;
        buf.push_back(one_byte);
    }
    amount_written += stream_a.sputn (buf.data(), buf.size());
    return amount_written;
}
