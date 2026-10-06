include(Catch)

add_executable(ap_unit_tests
    support/Golden.cpp
    support/WavFile.cpp
    unit/BuildInfoTests.cpp
    unit/ClipModelTests.cpp
    unit/ConcurrencyTests.cpp
    unit/DrumMachineTests.cpp
    unit/EngineTests.cpp
    unit/LinearSmoothedValueTests.cpp
    unit/MetronomeTests.cpp
    unit/MusicalTimeTests.cpp
    unit/OfflineRenderTests.cpp
    unit/ParameterTests.cpp
    unit/ProjectFileTests.cpp
    unit/ProjectModelTests.cpp
    unit/RecordingTests.cpp
    unit/RenderGraphTests.cpp
    unit/ResamplerTests.cpp
    unit/SamplerTests.cpp
    unit/SineOscillatorTests.cpp
    unit/SynthDspTests.cpp
    unit/SynthTests.cpp
    unit/TimelinePlaybackTests.cpp
    unit/TransportTests.cpp)

target_include_directories(ap_unit_tests PRIVATE support)
target_compile_definitions(ap_unit_tests PRIVATE
    AP_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
    AP_FUZZ_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus/project")
target_link_libraries(ap_unit_tests PRIVATE ap::core ap::project_options nlohmann_json::nlohmann_json Catch2::Catch2WithMain)

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
        desktop/AudioRecorderTests.cpp
        desktop/BridgeCodecTests.cpp
        desktop/NoteCaptureTests.cpp
        desktop/NoteRecorderTests.cpp
        desktop/SampleLoaderTests.cpp
        desktop/SessionTests.cpp
        "${PROJECT_SOURCE_DIR}/apps/desktop/src/AudioRecorder.cpp"
        "${PROJECT_SOURCE_DIR}/apps/desktop/src/NoteCapture.cpp"
        "${PROJECT_SOURCE_DIR}/apps/desktop/src/NoteRecorder.cpp"
        "${PROJECT_SOURCE_DIR}/apps/desktop/src/SampleLoader.cpp"
        "${PROJECT_SOURCE_DIR}/apps/desktop/src/Session.cpp"
        "${PROJECT_SOURCE_DIR}/apps/desktop/src/generated/BridgeCodec.cpp")
    target_include_directories(ap_desktop_tests PRIVATE "${PROJECT_SOURCE_DIR}/apps/desktop/src" support)
    target_compile_definitions(ap_desktop_tests PRIVATE
        JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ap_desktop_tests PRIVATE
        ap::core
        juce::juce_audio_formats
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        Catch2::Catch2WithMain)
    get_target_property(_ap_test_opts ap_project_options INTERFACE_COMPILE_OPTIONS)
    set_source_files_properties(desktop/AudioRecorderTests.cpp desktop/BridgeCodecTests.cpp desktop/NoteCaptureTests.cpp desktop/NoteRecorderTests.cpp desktop/SampleLoaderTests.cpp desktop/SessionTests.cpp
        PROPERTIES COMPILE_OPTIONS "${_ap_test_opts}")
    catch_discover_tests(ap_desktop_tests)
endif()

# Fuzz targets (Clang with libFuzzer; see the "fuzz" preset and CI job).
option(AP_BUILD_FUZZERS "Build libFuzzer targets" OFF)
if(AP_BUILD_FUZZERS)
    add_executable(ap_fuzz_project_loader fuzz/ProjectLoaderFuzz.cpp)
    target_link_libraries(ap_fuzz_project_loader PRIVATE ap::core ap::project_options)
    target_compile_options(ap_fuzz_project_loader PRIVATE -fsanitize=fuzzer)
    target_link_options(ap_fuzz_project_loader PRIVATE -fsanitize=fuzzer)
endif()
