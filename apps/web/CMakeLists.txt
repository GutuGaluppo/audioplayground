# The browser host (ADR-016): the core and the shared intent code compiled to a standalone
# WebAssembly module that an AudioWorklet runs. Build with Emscripten:
#
#   tools/web/build.sh        (or: emcmake cmake -S . -B build/web -DAP_BUILD_WEB=ON ...)
#
# The generated bridge codec is the desktop one, compiled unchanged against a tiny stand-in for the
# part of JUCE's juce_core it uses (src/compat), so the page and the module share one schema.

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "AP_BUILD_WEB needs Emscripten: use emcmake (see tools/web/build.sh)")
endif()

set(AP_WEB_SOURCES
    src/WebHost.cpp
    src/Exports.cpp
    src/compat/JsonText.cpp
    "${PROJECT_SOURCE_DIR}/apps/desktop/src/generated/BridgeCodec.cpp")

add_executable(ap_web ${AP_WEB_SOURCES})
target_include_directories(ap_web PRIVATE
    src
    src/compat
    "${PROJECT_SOURCE_DIR}/apps/desktop/src/generated")
target_link_libraries(ap_web PRIVATE ap::core ap::project_options nlohmann_json::nlohmann_json)

# Standalone: no JavaScript glue, just the .wasm and the functions in Exports.cpp.
set(AP_WEB_EXPORTS
    _ap_alloc _ap_free _ap_create _ap_intent _ap_process _ap_events _ap_events_size _ap_set_output_latency
    _ap_project_export _ap_project_text _ap_project_import _ap_project_error _ap_project_error_size
    _ap_project_saved _ap_project_unsaved)
list(JOIN AP_WEB_EXPORTS "," AP_WEB_EXPORT_LIST)
target_link_options(ap_web PRIVATE
    -sSTANDALONE_WASM=1
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=33554432
    -sSTACK_SIZE=1048576
    -sEXPORTED_FUNCTIONS=${AP_WEB_EXPORT_LIST}
    "-sEXPORTED_RUNTIME_METHODS=[]"
    -Wl,--no-entry)
set_target_properties(ap_web PROPERTIES SUFFIX ".wasm")
