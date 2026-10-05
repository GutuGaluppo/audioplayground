# Desktop shell: owns the window and (later) the WebView UI and audio device manager.
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
    HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input)

set(AP_DESKTOP_SOURCES
    src/AudioDeviceHost.cpp
    src/Main.cpp
    src/MainComponent.cpp)

target_sources(ap_desktop PRIVATE ${AP_DESKTOP_SOURCES})

target_compile_definitions(ap_desktop PRIVATE
    JUCE_WEB_BROWSER=0      # Enabled in Task 004 (WebView shell)
    JUCE_USE_CURL=0         # No network access from the app
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ap_desktop,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ap_desktop,JUCE_VERSION>")

target_link_libraries(ap_desktop
    PRIVATE
        ap::core
        juce::juce_audio_devices
        juce::juce_audio_utils
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# First-party warnings apply to our sources only, not to JUCE module sources compiled into this target.
get_target_property(_ap_desktop_opts ap_project_options INTERFACE_COMPILE_OPTIONS)
set_source_files_properties(${AP_DESKTOP_SOURCES}
    PROPERTIES COMPILE_OPTIONS "${_ap_desktop_opts}")
