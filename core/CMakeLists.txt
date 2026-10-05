# ap_core: headless engine core (model, engine, DSP, I/O).
# Must never depend on UI code, the WebView bridge, or platform windowing.

add_library(ap_core STATIC
    src/BuildInfo.cpp
    src/engine/Engine.cpp
    src/engine/Metronome.cpp
    src/engine/OfflineRenderer.cpp
    src/engine/Transport.cpp
    src/model/Commands.cpp
    src/model/Project.cpp
    src/model/ProjectDocument.cpp)
add_library(ap::core ALIAS ap_core)

target_include_directories(ap_core PUBLIC include)
target_link_libraries(ap_core PRIVATE ap::project_options)

set_source_files_properties(src/BuildInfo.cpp PROPERTIES COMPILE_DEFINITIONS
    "AP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR};AP_VERSION_MINOR=${PROJECT_VERSION_MINOR};AP_VERSION_PATCH=${PROJECT_VERSION_PATCH};AP_VERSION_STRING=\"${PROJECT_VERSION}\"")
