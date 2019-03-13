#include <logos/lib/utility.hpp>

#include <sstream>
#include <iomanip>

std::string string_to_hex_str(const std::string& input)
{
    static const char* const lut = "0123456789ABCDEF";
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
