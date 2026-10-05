#pragma once

#include <string_view>

namespace ap::core
{

struct BuildInfo
{
    int major;
    int minor;
    int patch;
    std::string_view versionString;
};

// Real-time safe: returns compile-time constants, no allocation.
[[nodiscard]] BuildInfo buildInfo() noexcept;

} // namespace ap::core
