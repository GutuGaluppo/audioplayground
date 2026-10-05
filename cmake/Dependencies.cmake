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

# JSON for the project file format (untrusted input: parsed with size and depth limits, ADR-006).
FetchContent_Declare(nlohmann_json
    URL      "https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz"
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

if(AP_BUILD_TESTS)
    FetchContent_Declare(Catch2
        URL      "https://github.com/catchorg/Catch2/archive/refs/tags/v3.9.1.tar.gz"
        URL_HASH SHA256=a215c2a723bd7483efd236dc86066842a389cb4e344c61119c978acdf24d39be
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SYSTEM)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

# Microsoft Edge WebView2 SDK (Windows only): headers + static loader for the WebView UI (ADR-002).
# Extracted into a folder whose name JUCE's FindWebView2 recognises.
if(AP_BUILD_DESKTOP AND WIN32)
    set(_ap_webview2_root "${CMAKE_BINARY_DIR}/_deps/webview2")
    FetchContent_Declare(WebView2
        URL      "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.3485.44"
        URL_HASH SHA256=bc09150b179246ac90189649b13be8e6b11b3ac200e817e18df106e1f3cf489e
        DOWNLOAD_NAME "Microsoft.Web.WebView2.1.0.3485.44.zip"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_DIR "${_ap_webview2_root}/Microsoft.Web.WebView2.1.0.3485.44")
    FetchContent_MakeAvailable(WebView2)
    set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${_ap_webview2_root}" CACHE PATH "WebView2 NuGet package folder" FORCE)
endif()
