# Desktop shell: owns the window, the WebView UI and the audio device manager.
# All audio/model logic lives in ap::core.

# The bundle ID is a persisted identifier (preferences, permissions, signing).
# Free to change until the first public release; see ADR-001.
set(AP_BUNDLE_ID "io.github.gutugaluppo.audioplayground" CACHE STRING "macOS bundle identifier")

juce_add_gui_app(ap_desktop
    PRODUCT_NAME "Audio Playground"
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "Audio Playground"
    BUNDLE_ID ${AP_BUNDLE_ID}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Audio Playground uses the microphone only while you record."
    HARDENED_RUNTIME_ENABLED TRUE
    HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input
    NEEDS_WEBVIEW2 TRUE)

# --- Web UI: build ui/ with pnpm, then embed ui/dist into the binary (ADR-002) -----------------

set(AP_UI_DIR "${PROJECT_SOURCE_DIR}/ui")
set(AP_UI_DIST_DIR "${AP_UI_DIR}/dist")
set(AP_UI_STAMP "${CMAKE_CURRENT_BINARY_DIR}/ui-build.stamp")
set(AP_EMBEDDED_UI_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedUi.cpp")

find_program(AP_PNPM pnpm REQUIRED)

# On Windows pnpm is a .cmd script; invoking it directly from MSBuild's generated batch file would
# end that batch file after pnpm returns, silently skipping the commands that follow.
if(WIN32)
    set(AP_PNPM_COMMAND cmd /c "${AP_PNPM}")
else()
    set(AP_PNPM_COMMAND "${AP_PNPM}")
endif()

file(GLOB_RECURSE AP_UI_INPUTS CONFIGURE_DEPENDS
    "${AP_UI_DIR}/src/*"
    "${AP_UI_DIR}/public/*"
    "${PROJECT_SOURCE_DIR}/presets/*")
list(APPEND AP_UI_INPUTS
    "${AP_UI_DIR}/index.html"
    "${AP_UI_DIR}/package.json"
    "${AP_UI_DIR}/tsconfig.json"
    "${AP_UI_DIR}/vite.config.ts"
    "${PROJECT_SOURCE_DIR}/pnpm-lock.yaml")

add_custom_command(
    OUTPUT "${AP_UI_STAMP}"
    COMMAND ${AP_PNPM_COMMAND} --dir "${AP_UI_DIR}" run build
    COMMAND "${CMAKE_COMMAND}" -E touch "${AP_UI_STAMP}"
    DEPENDS ${AP_UI_INPUTS}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Building web UI"
    VERBATIM)

add_custom_command(
    OUTPUT "${AP_EMBEDDED_UI_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
        -DINPUT_DIR=${AP_UI_DIST_DIR}
        -DOUTPUT=${AP_EMBEDDED_UI_SOURCE}
        -DNAMESPACE=ap::desktop::ui
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedDirectory.cmake"
    DEPENDS "${AP_UI_STAMP}" "${PROJECT_SOURCE_DIR}/cmake/EmbedDirectory.cmake"
    COMMENT "Embedding web UI"
    VERBATIM)

# --- Sources -------------------------------------------------------------------------------------

set(AP_DESKTOP_SOURCES
    src/AudioDeviceHost.cpp
    src/Main.cpp
    src/AudioRecorder.cpp
    src/Exporter.cpp
    src/NoteCapture.cpp
    src/NoteRecorder.cpp
    src/ProjectActions.cpp
    src/SampleLoader.cpp
    src/Session.cpp
    src/WebUiHost.cpp
    src/WebUiHostTimeline.cpp
    src/generated/BridgeCodec.cpp)

target_sources(ap_desktop PRIVATE ${AP_DESKTOP_SOURCES} "${AP_EMBEDDED_UI_SOURCE}")
target_include_directories(ap_desktop PRIVATE src)

target_compile_definitions(ap_desktop PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0         # No network access from the app
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ap_desktop,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ap_desktop,JUCE_VERSION>")

target_link_libraries(ap_desktop
    PRIVATE
        ap::core
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# First-party warnings apply to our sources only, not to JUCE module sources compiled into this target.
get_target_property(_ap_desktop_opts ap_project_options INTERFACE_COMPILE_OPTIONS)
set_source_files_properties(${AP_DESKTOP_SOURCES}
    PROPERTIES COMPILE_OPTIONS "${_ap_desktop_opts}")
