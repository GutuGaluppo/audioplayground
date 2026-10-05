include(Catch)

add_executable(ap_unit_tests
    support/Golden.cpp
    support/WavFile.cpp
    unit/BuildInfoTests.cpp
    unit/EngineTests.cpp
    unit/LinearSmoothedValueTests.cpp
    unit/OfflineRenderTests.cpp
    unit/SineOscillatorTests.cpp)

target_include_directories(ap_unit_tests PRIVATE support)
target_compile_definitions(ap_unit_tests PRIVATE AP_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
target_link_libraries(ap_unit_tests PRIVATE ap::core ap::project_options Catch2::Catch2WithMain)

catch_discover_tests(ap_unit_tests)

if("realtime" IN_LIST AP_SANITIZERS)
    add_executable(ap_rtsan_smoke rtsan/RtsanSmoke.cpp)
    target_compile_options(ap_rtsan_smoke PRIVATE -fsanitize=realtime)
    target_link_options(ap_rtsan_smoke PRIVATE -fsanitize=realtime)
    add_test(NAME rtsan_catches_allocation COMMAND ap_rtsan_smoke)
    set_tests_properties(rtsan_catches_allocation PROPERTIES WILL_FAIL TRUE)
endif()
