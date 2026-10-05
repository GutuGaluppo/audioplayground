#include "ap/core/BuildInfo.h"

// AP_VERSION_* are defined by core/CMakeLists.txt from the CMake project version.

namespace ap::core
{

BuildInfo buildInfo() noexcept
{
    return {AP_VERSION_MAJOR, AP_VERSION_MINOR, AP_VERSION_PATCH, AP_VERSION_STRING};
}

} // namespace ap::core
