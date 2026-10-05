#include "ap/core/BuildInfo.h"

#include <catch2/catch_test_macros.hpp>
#include <string>

TEST_CASE ("buildInfo version string matches numeric components", "[core]")
{
    const auto info = ap::core::buildInfo();

    const auto expected
        = std::to_string (info.major) + "." + std::to_string (info.minor) + "." + std::to_string (info.patch);

    CHECK (info.versionString == expected);
}
