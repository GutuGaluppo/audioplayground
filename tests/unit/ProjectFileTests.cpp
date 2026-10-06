#include "TempDirectory.h"
#include "ap/io/ProjectFiles.h"
#include "ap/model/ClipEditing.h"
#include "ap/model/ProjectDocument.h"
#include "ap/model/ProjectSerialization.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>

using namespace ap::model;
using namespace ap::io;
using ap::test::TempDirectory;

namespace
{
Project sampleProject()
{
    ProjectDocument doc;
    REQUIRE (doc.perform (RenameProject {"Night Drive \xC3\xA9"}));
    REQUIRE (doc.perform (SetTempo {93.5}));
    REQUIRE (doc.perform (SetTimeSignature {ap::core::TimeSignature {6, 8}}));
    REQUIRE (doc.perform (AddTrack {TrackKind::audio, "Vocals"}));
    REQUIRE (doc.perform (AddTrack {TrackKind::instrument}));
    REQUIRE (doc.perform (SetTrackVolume {doc.project().tracks[0].id, -4.5f}));
    REQUIRE (doc.perform (SetTrackPan {doc.project().tracks[1].id, 0.25f}));
    REQUIRE (doc.perform (SetTrackMute {doc.project().tracks[1].id, true}));
    REQUIRE (doc.perform (SetParameter {ap::params::ParamId::toneLevel, -30.0f}));
    REQUIRE (doc.perform (AddAsset {"audio/1-kick.wav", "kick.wav"}));
    REQUIRE (doc.perform (SetSamplerAsset {doc.project().assets[0].id}));
    REQUIRE (doc.perform (SetDrumPad {1, DrumPad {doc.project().assets[0].id, -6.0f, 2.0f, true}}));

    Clip audio;
    audio.start = 1920;
    audio.length = 7680;
    audio.asset = doc.project().assets[0].id;
    audio.sourceOffset = 352'800'000; // 0.5 s
    REQUIRE (doc.perform (AddClip {doc.project().tracks[0].id, audio}));

    Clip melody;
    melody.length = 3840;
    melody.contentOffset = 240;
    melody.notes = {{0, 480, 60, 0.8f}, {480, 240, 64, 1.0f}, {480, 960, 67, 0.5f}};
    REQUIRE (doc.perform (AddClip {doc.project().tracks[1].id, melody}));

    REQUIRE (doc.perform (AddTrack {InstrumentKind::drums}));
    auto pattern = makePatternClip (3840, 4 * 3840);
    pattern = withDrumStep (pattern, 0, 0, true);
    pattern = withDrumStep (pattern, 2, 8, true);
    REQUIRE (doc.perform (AddClip {doc.project().tracks[2].id, pattern}));

    auto eq = defaultEffectState (ap::params::EffectKind::eq);
    eq.enabled = true;
    eq.values[static_cast<std::size_t> (ap::params::EqParam::midGain)] = -4.5f;
    REQUIRE (doc.perform (SetTrackEffect {doc.project().tracks[0].id, ap::params::EffectKind::eq, eq}));
    auto delay = defaultEffectState (ap::params::EffectKind::delay);
    delay.enabled = true;
    delay.values[static_cast<std::size_t> (ap::params::DelayParam::time)] = 250.0f;
    REQUIRE (doc.perform (SetTrackEffect {doc.project().tracks[2].id, ap::params::EffectKind::delay, delay}));
    return doc.project();
}

const ProjectMetadata metadata {"2026-10-05T19:00:00Z", "2026-10-05T19:30:00Z"};

const LoadError& errorOf (const LoadResult& result)
{
    REQUIRE (std::holds_alternative<LoadError> (result));
    return std::get<LoadError> (result);
}

std::string mutate (const std::function<void (nlohmann::json&)>& change)
{
    auto json = nlohmann::json::parse (serialiseProject (sampleProject(), metadata));
    change (json);
    return json.dump();
}
} // namespace

TEST_CASE ("A project round-trips through JSON exactly and byte-stably", "[persistence]")
{
    const auto project = sampleProject();
    const auto text = serialiseProject (project, metadata);

    const auto loaded = parseProject (text);
    REQUIRE (std::holds_alternative<LoadedProject> (loaded));
    CHECK (std::get<LoadedProject> (loaded).project == project);
    CHECK (std::get<LoadedProject> (loaded).metadata == metadata);

    // save -> load -> save produces identical bytes
    CHECK (serialiseProject (std::get<LoadedProject> (loaded).project, metadata) == text);
}

TEST_CASE ("Track effects load with defaults for anything missing and refuse anything unknown",
           "[persistence]")
{
    // Files written before effects existed have no "effects" key.
    const auto old = parseProject (mutate (
        [] (nlohmann::json& json)
        {
            for (auto& track : json["tracks"])
                track.erase ("effects");
        }));
    REQUIRE (std::holds_alternative<LoadedProject> (old));
    for (const auto& track : std::get<LoadedProject> (old).project.tracks)
        CHECK (track.effects == defaultTrackEffects());

    const auto partial = parseProject (mutate (
        [] (nlohmann::json& json) { json["tracks"][0]["effects"] = {{"reverb", {{"enabled", true}}}}; }));
    REQUIRE (std::holds_alternative<LoadedProject> (partial));
    const auto& effects = std::get<LoadedProject> (partial).project.tracks[0].effects;
    CHECK (effects[static_cast<std::size_t> (ap::params::EffectKind::reverb)].enabled);
    CHECK_FALSE (effects[static_cast<std::size_t> (ap::params::EffectKind::eq)].enabled);

    CHECK (std::holds_alternative<LoadError> (parseProject (mutate (
        [] (nlohmann::json& json) { json["tracks"][0]["effects"]["chorus"] = nlohmann::json::object(); }))));
    CHECK (std::holds_alternative<LoadError> (parseProject (
        mutate ([] (nlohmann::json& json) { json["tracks"][0]["effects"]["eq"]["sparkle"] = 1; }))));
    CHECK (std::holds_alternative<LoadError> (parseProject (
        mutate ([] (nlohmann::json& json) { json["tracks"][0]["effects"]["delay"]["time"] = 99999; }))));
    CHECK (std::holds_alternative<LoadError> (parseProject (
        mutate ([] (nlohmann::json& json) { json["tracks"][0]["effects"]["delay"]["enabled"] = "yes"; }))));
}

TEST_CASE ("Parameters unknown to this build are preserved", "[persistence]")
{
    const auto text = mutate ([] (nlohmann::json& j) { j["parameters"]["reverb.size"] = 0.75; });
    const auto loaded = parseProject (text);
    REQUIRE (std::holds_alternative<LoadedProject> (loaded));

    const auto& project = std::get<LoadedProject> (loaded).project;
    REQUIRE (project.preservedParameters.size() == 1);
    CHECK (project.preservedParameters[0].first == "reverb.size");

    const auto resaved = nlohmann::json::parse (serialiseProject (project, metadata));
    CHECK (resaved["parameters"]["reverb.size"] == 0.75);
}

TEST_CASE ("Missing known parameters fall back to their defaults", "[persistence]")
{
    const auto text = mutate ([] (nlohmann::json& j) { j["parameters"] = nlohmann::json::object(); });
    const auto loaded = parseProject (text);
    REQUIRE (std::holds_alternative<LoadedProject> (loaded));
    CHECK (std::get<LoadedProject> (loaded).project.parameters == Project::defaultParameterValues());
}

TEST_CASE ("Projects from a newer version are refused, not partially loaded", "[persistence]")
{
    const auto text = mutate ([] (nlohmann::json& j) { j["schemaVersion"] = currentSchemaVersion + 1; });
    CHECK (errorOf (parseProject (text)).code == LoadError::Code::newerVersion);
}

TEST_CASE ("Malformed and hostile project files are rejected", "[persistence][security]")
{
    using Change = std::function<void (nlohmann::json&)>;
    const std::vector<std::pair<const char*, Change>> cases {
        {"wrong format tag", [] (auto& j) { j["format"] = "something"; }},
        {"unknown top-level key", [] (auto& j) { j["extra"] = 1; }},
        {"missing tracks", [] (auto& j) { j.erase ("tracks"); }},
        {"tempo too high", [] (auto& j) { j["tempo"] = 999; }},
        {"tempo as text", [] (auto& j) { j["tempo"] = "120"; }},
        {"bad time signature", [] (auto& j) { j["timeSignature"] = {5, 3}; }},
        {"time signature not a pair", [] (auto& j) { j["timeSignature"] = {4}; }},
        {"absurd export rate", [] (auto& j) { j["exportSampleRate"] = 1; }},
        {"negative track id", [] (auto& j) { j["tracks"][0]["id"] = -1; }},
        {"fractional track id", [] (auto& j) { j["tracks"][0]["id"] = 1.5; }},
        {"duplicate track ids", [] (auto& j) { j["tracks"][1]["id"] = j["tracks"][0]["id"]; }},
        {"nextTrackId not above ids", [] (auto& j) { j["nextTrackId"] = 1; }},
        {"unknown track kind", [] (auto& j) { j["tracks"][0]["kind"] = "midi"; }},
        {"empty track name", [] (auto& j) { j["tracks"][0]["name"] = "  "; }},
        {"huge track name", [] (auto& j) { j["tracks"][0]["name"] = std::string (10000, 'x'); }},
        {"volume out of range", [] (auto& j) { j["tracks"][0]["volumeDb"] = 50; }},
        {"pan out of range", [] (auto& j) { j["tracks"][0]["pan"] = 2; }},
        {"mute as number", [] (auto& j) { j["tracks"][0]["muted"] = 1; }},
        {"extra track key", [] (auto& j) { j["tracks"][0]["path"] = "/etc/passwd"; }},
        {"known parameter out of range", [] (auto& j) { j["parameters"]["tone.level"] = 100; }},
        {"malformed parameter id", [] (auto& j) { j["parameters"]["../../x"] = 1; }},
        {"bad timestamp", [] (auto& j) { j["createdAt"] = "yesterday<script>"; }},
        {"asset path traversal", [] (auto& j) { j["assets"][0]["path"] = "audio/../../etc/passwd"; }},
        {"asset absolute path", [] (auto& j) { j["assets"][0]["path"] = "/etc/passwd"; }},
        {"asset outside audio folder", [] (auto& j) { j["assets"][0]["path"] = "project.json"; }},
        {"asset hidden file", [] (auto& j) { j["assets"][0]["path"] = "audio/.secret"; }},
        {"duplicate asset ids", [] (auto& j) { j["assets"].push_back (j["assets"][0]); }},
        {"nextAssetId too low", [] (auto& j) { j["nextAssetId"] = 1; }},
        {"sampler asset missing", [] (auto& j) { j["samplerAsset"] = 99; }},
        {"sampler asset negative", [] (auto& j) { j["samplerAsset"] = -1; }},
        {"drum kit too small", [] (auto& j) { j["drums"]["pads"].erase (0); }},
        {"old drum steps", [] (auto& j) { j["drums"]["steps"] = nlohmann::json::array(); }},
        {"two tracks for one instrument", [] (auto& j) { j["tracks"][2]["instrument"] = "synth"; }},
        {"unknown instrument", [] (auto& j) { j["tracks"][1]["instrument"] = "theremin"; }},
        {"audio track with instrument", [] (auto& j) { j["tracks"][0]["instrument"] = "synth"; }},
        {"instrument track without instrument", [] (auto& j) { j["tracks"][1].erase ("instrument"); }},
        {"clips not a list", [] (auto& j) { j["tracks"][0]["clips"] = 3; }},
        {"duplicate clip ids",
         [] (auto& j) { j["tracks"][2]["clips"][0]["id"] = j["tracks"][1]["clips"][0]["id"]; }},
        {"nextClipId too low", [] (auto& j) { j["nextClipId"] = 1; }},
        {"clip past the end of the timeline",
         [] (auto& j) { j["tracks"][1]["clips"][0]["start"] = 400000000; }},
        {"clip too short", [] (auto& j) { j["tracks"][1]["clips"][0]["length"] = 0; }},
        {"negative clip start", [] (auto& j) { j["tracks"][1]["clips"][0]["start"] = -1; }},
        {"fractional clip start", [] (auto& j) { j["tracks"][1]["clips"][0]["start"] = 0.5; }},
        {"audio clip missing asset", [] (auto& j) { j["tracks"][0]["clips"][0]["asset"] = 99; }},
        {"audio clip negative offset", [] (auto& j) { j["tracks"][0]["clips"][0]["sourceOffset"] = -5; }},
        {"audio clip with notes",
         [] (auto& j) { j["tracks"][0]["clips"][0]["notes"] = nlohmann::json::array(); }},
        {"note clip with asset", [] (auto& j) { j["tracks"][1]["clips"][0]["asset"] = 1; }},
        {"note pitch out of range", [] (auto& j) { j["tracks"][1]["clips"][0]["notes"][0][2] = 128; }},
        {"note length zero", [] (auto& j) { j["tracks"][1]["clips"][0]["notes"][0][1] = 0; }},
        {"note velocity zero", [] (auto& j) { j["tracks"][1]["clips"][0]["notes"][0][3] = 0; }},
        {"note as object", [] (auto& j) { j["tracks"][1]["clips"][0]["notes"][0] = {{"pitch", 60}}; }},
        {"loop too short", [] (auto& j) { j["tracks"][2]["clips"][0]["loopLength"] = 1; }},
        {"drum pad missing asset", [] (auto& j) { j["drums"]["pads"][0]["sample"] = 42; }},
        {"drum pad volume too high", [] (auto& j) { j["drums"]["pads"][0]["volumeDb"] = 20; }},
        {"drum pad extra key", [] (auto& j) { j["drums"]["pads"][0]["file"] = "x"; }},
        {"too many tracks",
         [] (auto& j)
         {
             for (int i = 0; i < 70; ++i)
             {
                 auto t = j["tracks"][0];
                 t["id"] = 100 + i;
                 j["tracks"].push_back (t);
             }
             j["nextTrackId"] = 1000;
         }},
    };

    for (const auto& [label, change] : cases)
    {
        INFO (label);
        const auto result = parseProject (mutate (change));
        REQUIRE (std::holds_alternative<LoadError> (result));
        CHECK (std::get<LoadError> (result).code == LoadError::Code::invalid);
    }
}

TEST_CASE ("Non-JSON, oversized and deeply nested input is rejected safely", "[persistence][security]")
{
    CHECK (errorOf (parseProject ("")).code == LoadError::Code::notJson);
    CHECK (errorOf (parseProject ("{\"format\":")).code == LoadError::Code::notJson);
    CHECK (errorOf (parseProject ("[]")).code == LoadError::Code::invalid);
    CHECK (errorOf (parseProject ("\xFF\xFE garbage")).code == LoadError::Code::notJson);

    const std::string deep = std::string (100000, '[') + std::string (100000, ']');
    CHECK (errorOf (parseProject (deep)).code == LoadError::Code::tooDeeplyNested);

    // Brackets inside strings do not count towards nesting.
    const auto text = mutate ([] (nlohmann::json& j) { j["name"] = std::string (60, '['); });
    CHECK (std::holds_alternative<LoadedProject> (parseProject (text)));

    const std::string huge (maxProjectFileBytes + 1, ' ');
    CHECK (errorOf (parseProject (huge)).code == LoadError::Code::tooLarge);
}

TEST_CASE ("Error messages never echo file contents", "[persistence][security]")
{
    const auto text = mutate ([] (nlohmann::json& j) { j["tracks"][0]["kind"] = "SECRET-CONTENT"; });
    CHECK (errorOf (parseProject (text)).message.find ("SECRET") == std::string::npos);
}

TEST_CASE ("Saving creates the folder layout, keeps a backup and loads back", "[persistence][io]")
{
    TempDirectory temp;
    const ProjectFolder folder {temp.path() / "Song.playground"};
    const auto project = sampleProject();

    REQUIRE_FALSE (saveProject (folder, project, metadata).has_value());
    CHECK (std::filesystem::is_directory (folder.audioDirectory()));
    CHECK (std::filesystem::is_directory (folder.autosaveDirectory()));
    CHECK_FALSE (std::filesystem::exists (folder.backupFile()));

    auto changed = project;
    changed.tempoBpm = 140.0;
    REQUIRE_FALSE (saveProject (folder, changed, metadata).has_value());

    const auto current = loadProject (folder);
    REQUIRE (std::holds_alternative<LoadedProject> (current));
    CHECK (std::get<LoadedProject> (current).project.tempoBpm == 140.0);

    const auto backup = loadBackup (folder);
    REQUIRE (std::holds_alternative<LoadedProject> (backup));
    CHECK (std::get<LoadedProject> (backup).project == project);

    // No temporary files are left behind.
    for (const auto& entry : std::filesystem::directory_iterator (folder.root))
        CHECK (entry.path().filename().string().find (".tmp-") == std::string::npos);
}

TEST_CASE ("Loading reports a missing project without touching the disk", "[persistence][io]")
{
    TempDirectory temp;
    const ProjectFolder folder {temp.path() / "Missing.playground"};
    const auto result = loadProject (folder);
    REQUIRE (std::holds_alternative<LoadError> (result));
    CHECK_FALSE (std::filesystem::exists (folder.root));
}

TEST_CASE ("Autosaves rotate and never touch project.json", "[persistence][io]")
{
    TempDirectory temp;
    const ProjectFolder folder {temp.path() / "Song.playground"};
    REQUIRE_FALSE (saveProject (folder, sampleProject(), metadata).has_value());
    const auto saved = readFileLimited (folder.projectFile(), maxProjectFileBytes);

    auto project = sampleProject();
    for (int i = 0; i < 8; ++i)
    {
        project.tempoBpm = 100.0 + i;
        REQUIRE_FALSE (writeAutosave (folder, project, metadata).has_value());
    }

    const auto autosaves = listAutosaves (folder);
    REQUIRE (autosaves.size() == maxAutosaves);

    const auto newest = readFileLimited (autosaves.front(), maxProjectFileBytes);
    const auto loaded = parseProject (std::get<std::string> (newest));
    REQUIRE (std::holds_alternative<LoadedProject> (loaded));
    CHECK (std::get<LoadedProject> (loaded).project.tempoBpm == 107.0);

    CHECK (readFileLimited (folder.projectFile(), maxProjectFileBytes) == saved);
}

TEST_CASE ("readFileLimited refuses files over the limit", "[persistence][io]")
{
    TempDirectory temp;
    const auto path = temp.path() / "big.json";
    std::ofstream (path) << std::string (1000, 'x');
    CHECK (std::holds_alternative<IoError> (readFileLimited (path, 999)));
    CHECK (std::get<std::string> (readFileLimited (path, 1000)).size() == 1000);
}

TEST_CASE ("Asset paths cannot escape the project folder", "[persistence][security]")
{
    TempDirectory temp;
    const ProjectFolder folder {temp.path() / "Song.playground"};
    std::filesystem::create_directories (folder.audioDirectory());

    const auto ok = resolveAssetPath (folder, "audio/take-1.wav");
    REQUIRE (ok.has_value());
    CHECK (ok->filename() == "take-1.wav");

    for (const char* bad :
         {"", "/etc/passwd", "../outside.wav", "audio/../../outside.wav", "audio/./x.wav", "audio//x.wav",
          "C:\\Windows\\win.ini", "audio\\..\\x.wav", "audio/x?.wav", "\x01.wav", ".", ".."})
    {
        INFO (bad);
        CHECK_FALSE (resolveAssetPath (folder, bad).has_value());
    }

#if !defined(_WIN32)
    // A symbolic link inside the project that points outside it is refused.
    TempDirectory outside;
    std::filesystem::create_directory_symlink (outside.path(), folder.audioDirectory() / "link");
    CHECK_FALSE (resolveAssetPath (folder, "audio/link/secret.wav").has_value());
#endif
}

TEST_CASE ("Fuzz seed corpus files are valid projects", "[persistence]")
{
    for (const auto& entry : std::filesystem::directory_iterator (AP_FUZZ_CORPUS_DIR))
    {
        INFO (entry.path().filename().string());
        const auto text = readFileLimited (entry.path(), maxProjectFileBytes);
        REQUIRE (std::holds_alternative<std::string> (text));
        CHECK (std::holds_alternative<LoadedProject> (parseProject (std::get<std::string> (text))));
    }
}
