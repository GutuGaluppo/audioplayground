include(Catch)

add_executable(ap_unit_tests
    unit/BuildInfoTests.cpp
    unit/EngineTests.cpp
    unit/LinearSmoothedValueTests.cpp
    unit/SineOscillatorTests.cpp)

target_include_directories(ap_unit_tests PRIVATE support)
target_link_libraries(ap_unit_tests PRIVATE ap::core ap::project_options Catch2::Catch2WithMain)

catch_discover_tests(ap_unit_tests)
