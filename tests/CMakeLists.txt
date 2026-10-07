include(Catch)

add_executable(ap_unit_tests
    support/Golden.cpp
    support/WavFile.cpp
    unit/AccompanimentTests.cpp
    unit/BuildInfoTests.cpp
    unit/ClipModelTests.cpp
    unit/CompressorTests.cpp
    unit/ConcurrencyTests.cpp
    unit/DelayTests.cpp
    unit/DistortionTests.cpp
    unit/BusTests.cpp
    unit/DrumMachineTests.cpp
    unit/EffectChainTests.cpp
    unit/IntentApplierTests.cpp
    unit/EngineTests.cpp
    unit/EqualizerTests.cpp
    unit/ExportTests.cpp
    unit/FilterTests.cpp
    unit/LinearSmoothedValueTests.cpp
    unit/MetronomeTests.cpp
    unit/MusicalTimeTests.cpp
    unit/OfflineRenderTests.cpp
    unit/ParameterTests.cpp
    unit/PeaksTests.cpp
    unit/ProjectFileTests.cpp
    unit/ProjectModelTests.cpp
    unit/RecordingTests.cpp
    unit/ReverbTests.cpp
    unit/RenderGraphTests.cpp
    unit/ResamplerTests.cpp
    unit/RhythmAnalysisTests.cpp
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
        desktop/AudioDeviceHostTests.cpp
        support/WavFile.cpp
        desktop/BridgeIntegrationTests.cpp
        desktop/AudioRecorderTests.cpp
        desktop/BridgeCodecTests.cpp
        desktop/ExporterTests.cpp
        desktop/NoteCaptureTests.cpp
        desktop/NoteRecorderTests.cpp
        desktop/SampleLoaderTests.cpp
        desktop/SessionTests.cpp
        "${PROJECT_SOURCE_DIR}/apps/desktop/src/AccompanimentService.cpp"
        "${PROJECT_SOURCE_DIR}/apps/desktop/src/AudioDeviceHost.cpp"
        "${PROJECT_SOURCE_DIR}/apps/desktop/src/AudioRecorder.cpp"
        "${PROJECT_SOURCE_DIR}/apps/desktop/src/Exporter.cpp"
        "${PROJECT_SOURCE_DIR}/apps/desktop/src/NoteCapture.cpp"
        "${PROJECT_SOURCE_DIR}/apps/desktop/src/NoteRecorder.cpp"
        "${PROJECT_SOURCE_DIR}/apps/desktop/src/ProjectActions.cpp"
        "${PROJECT_SOURCE_DIR}/apps/desktop/src/SampleLoader.cpp"
        "${PROJECT_SOURCE_DIR}/apps/desktop/src/Session.cpp"
        "${PROJECT_SOURCE_DIR}/apps/desktop/src/WebUiHost.cpp"
        "${PROJECT_SOURCE_DIR}/apps/desktop/src/WebUiHostTimeline.cpp"
        "${PROJECT_SOURCE_DIR}/apps/desktop/src/generated/BridgeCodec.cpp")
    target_include_directories(ap_desktop_tests PRIVATE "${PROJECT_SOURCE_DIR}/apps/desktop/src" support)
    target_compile_definitions(ap_desktop_tests PRIVATE
        AP_HEADLESS_UI=1
        JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ap_desktop_tests PRIVATE
        ap::core
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        Catch2::Catch2WithMain)
    get_target_property(_ap_test_opts ap_project_options INTERFACE_COMPILE_OPTIONS)
    set_source_files_properties(desktop/AudioDeviceHostTests.cpp desktop/BridgeIntegrationTests.cpp desktop/AudioRecorderTests.cpp desktop/BridgeCodecTests.cpp desktop/ExporterTests.cpp desktop/NoteCaptureTests.cpp desktop/NoteRecorderTests.cpp desktop/SampleLoaderTests.cpp desktop/SessionTests.cpp
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

# CPU budget of the reference project (plan §3.2). Meaningful in optimised builds only, so it is a
# test there and a plain executable elsewhere.
add_executable(ap_benchmarks benchmarks/ReferenceProject.cpp)
target_include_directories(ap_benchmarks PRIVATE support)
target_link_libraries(ap_benchmarks PRIVATE ap::core ap::project_options)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_test(NAME cpu_budget_reference_project COMMAND ap_benchmarks)
    set_tests_properties(cpu_budget_reference_project PROPERTIES LABELS benchmark)
endif()
