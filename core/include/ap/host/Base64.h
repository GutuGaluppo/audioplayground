#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ap::host
{

// RFC 4648 base64 with padding: how binary data (waveform peaks) travels inside a JSON event.
[[nodiscard]] std::string base64Encode (const std::uint8_t* data, std::size_t size);

} // namespace ap::host
