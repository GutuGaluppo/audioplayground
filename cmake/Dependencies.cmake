# Third-party dependencies, pinned by version AND content hash (see docs/THIRD_PARTY_LICENSES.md).
#
# To build against a local copy instead of downloading, set e.g.
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
# The local copy must be the same version as pinned here.

include(FetchContent)

set(AP_JUCE_VERSION "9.0.3")

if(AP_BUILD_DESKTOP)
    FetchContent_Declare(JUCE
        URL      "https://github.com/juce-framework/JUCE/archive/refs/tags/${AP_JUCE_VERSION}.tar.gz"
        URL_HASH SHA256=a81e5508b8a0efa483917794ebeaff56aed3075730c405a947734413f40c1aba
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SYSTEM)
    FetchContent_MakeAvailable(JUCE)
endif()

if(AP_BUILD_TESTS)
    FetchContent_Declare(Catch2
        URL      "https://github.com/catchorg/Catch2/archive/refs/tags/v3.9.1.tar.gz"
        URL_HASH SHA256=a215c2a723bd7483efd236dc86066842a389cb4e344c61119c978acdf24d39be
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SYSTEM)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
