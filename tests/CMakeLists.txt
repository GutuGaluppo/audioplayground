include(Catch)

add_executable(ap_unit_tests
    support/Golden.cpp
    support/WavFile.cpp
    unit/BuildInfoTests.cpp
    unit/ConcurrencyTests.cpp
    unit/EngineTests.cpp
    unit/LinearSmoothedValueTests.cpp
    unit/MetronomeTests.cpp
    unit/MusicalTimeTests.cpp
    unit/OfflineRenderTests.cpp
    unit/ParameterTests.cpp
    unit/ProjectModelTests.cpp
    unit/RenderGraphTests.cpp
    unit/SineOscillatorTests.cpp
    unit/TransportTests.cpp)

target_include_directories(ap_unit_tests PRIVATE support)
target_compile_definitions(ap_unit_tests PRIVATE AP_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
target_link_libraries(ap_unit_tests PRIVATE ap::core ap::project_options Catch2::Catch2WithMain)

catch_discover_tests(ap_unit_tests)

if("realtime" IN_LIST AP_SANITIZERS)
    add_executable(ap_rtsan_smoke rtsan/RtsanSmoke.cpp)
    target_compile_options(ap_rtsan_smoke PRIVATE -fsanitize=realtime)
    target_link_options(ap_rtsan_smoke PRIVATE -fsanitize=realtime)
    add_test(NAME rtsan_catches_allocation COMMAND ap_rtsan_smoke)
    set_tests_properties(rtsan_catches_allocation PROPERTIES PASS_REGULAR_EXPRESSION "RealtimeSanitizer")
endif()

# Desktop adapter tests (JUCE-based bridge codec). Only built with the desktop app.
if(AP_BUILD_DESKTOP)
    add_executable(ap_desktop_tests
        desktop/BridgeCodecTests.cpp
        "${PROJECT_SOURCE_DIR}/apps/desktop/src/generated/BridgeCodec.cpp")
    target_include_directories(ap_desktop_tests PRIVATE "${PROJECT_SOURCE_DIR}/apps/desktop/src")
    target_compile_definitions(ap_desktop_tests PRIVATE
        JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0)
    target_link_libraries(ap_desktop_tests PRIVATE
        ap::core
        juce::juce_core
        juce::juce_recommended_config_flags
        Catch2::Catch2WithMain)
    get_target_property(_ap_test_opts ap_project_options INTERFACE_COMPILE_OPTIONS)
    set_source_files_properties(desktop/BridgeCodecTests.cpp PROPERTIES COMPILE_OPTIONS "${_ap_test_opts}")
    catch_discover_tests(ap_desktop_tests)
endif()
