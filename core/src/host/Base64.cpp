#include "ap/host/Base64.h"

namespace ap::host
{

std::string base64Encode (const std::uint8_t* data, std::size_t size)
{
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve ((size + 2) / 3 * 4);
    for (std::size_t i = 0; i < size; i += 3)
    {
        const std::uint32_t a = data[i];
        const std::uint32_t b = i + 1 < size ? data[i + 1] : 0;
        const std::uint32_t c = i + 2 < size ? data[i + 2] : 0;
        const std::uint32_t triple = (a << 16) | (b << 8) | c;
        out.push_back (alphabet[(triple >> 18) & 63]);
        out.push_back (alphabet[(triple >> 12) & 63]);
        out.push_back (i + 1 < size ? alphabet[(triple >> 6) & 63] : '=');
        out.push_back (i + 2 < size ? alphabet[triple & 63] : '=');
    }
    return out;
}

} // namespace ap::host
