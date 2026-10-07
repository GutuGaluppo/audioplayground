# ap_core: headless engine core (model, engine, DSP, I/O).
# Must never depend on UI code, the WebView bridge, or platform windowing.

# The groove library (presets/grooves.json) is compiled in as bytes, so the app needs no data file.
set(AP_GROOVES_JSON "${PROJECT_SOURCE_DIR}/presets/grooves.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${AP_GROOVES_JSON}")
file(READ "${AP_GROOVES_JSON}" AP_GROOVES_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," AP_GROOVES_BYTES "${AP_GROOVES_HEX}")
configure_file("${PROJECT_SOURCE_DIR}/cmake/GrooveData.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/GrooveData.cpp" @ONLY)

add_library(ap_core STATIC
    src/BuildInfo.cpp
    src/accompaniment/Grooves.cpp
    src/accompaniment/MusicalContext.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/GrooveData.cpp"
    src/analysis/RhythmAnalysis.cpp
    src/dsp/Loudness.cpp
    src/dsp/Oversampler.cpp
    src/dsp/Peaks.cpp
    src/dsp/Resampler.cpp
    src/io/ProjectFiles.cpp
    src/io/Recordings.cpp
    src/io/WavExport.cpp
    src/fx/Compressor.cpp
    src/fx/Delay.cpp
    src/fx/Distortion.cpp
    src/fx/Equalizer.cpp
    src/fx/Filter.cpp
    src/fx/Limiter.cpp
    src/fx/Reverb.cpp
    src/engine/Engine.cpp
    src/engine/Export.cpp
    src/engine/InputCapture.cpp
    src/engine/Metronome.cpp
    src/engine/OfflineRenderer.cpp
    src/engine/RenderGraph.cpp
    src/engine/TimelinePlayer.cpp
    src/engine/TrackChain.cpp
    src/engine/Transport.cpp
    src/host/DocumentEditor.cpp
    src/host/IntentApplier.cpp
    src/host/Snapshots.cpp
    src/instruments/DrumMachine.cpp
    src/instruments/FactoryKit.cpp
    src/instruments/Sampler.cpp
    src/instruments/Synth.cpp
    src/model/ClipEditing.cpp
    src/model/Commands.cpp
    src/model/Project.cpp
    src/model/ProjectDocument.cpp
    src/model/ProjectSerialization.cpp)
add_library(ap::core ALIAS ap_core)

target_include_directories(ap_core PUBLIC include)
target_link_libraries(ap_core PRIVATE ap::project_options nlohmann_json::nlohmann_json)

set_source_files_properties(src/BuildInfo.cpp PROPERTIES COMPILE_DEFINITIONS
    "AP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR};AP_VERSION_MINOR=${PROJECT_VERSION_MINOR};AP_VERSION_PATCH=${PROJECT_VERSION_PATCH};AP_VERSION_STRING=\"${PROJECT_VERSION}\"")
