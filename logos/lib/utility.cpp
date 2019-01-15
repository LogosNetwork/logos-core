#include "utility.hpp"
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

std::string byte_vector_to_string (const std::vector<uint8_t> & buf)
{
    std::stringstream stream;
    for(size_t i = 0; i < buf.size(); ++i)
    {
        stream << std::hex << std::noshowbase << std::setw (2) << std::setfill ('0') << (unsigned int)(buf[i]);
    }
    return stream.str ();
}
