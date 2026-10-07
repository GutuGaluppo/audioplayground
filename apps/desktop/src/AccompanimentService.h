#pragma once

#include "SampleLoader.h"
#include "Session.h"
#include "ap/accompaniment/Grooves.h"
#include "ap/analysis/RhythmAnalysis.h"
#include "ap/engine/Engine.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ap::desktop
{

// Smart Accompaniment (ADR-011): after a take, finds a beat from the curated library that fits it,
// lets the musician hear it with the take, and only adds it on request.
//
// Nothing here changes the project until add(): the take is only read, a preview plays a copy of
// the project through the engine (Session::setPreview), and adding is one normal undo step. The
// analysis runs on a worker thread and never delays playback. Message thread only.
class AccompanimentService
{
public:
    enum class State
    {
        idle = 0,
        analyzing = 1,
        suggestionReady = 2,
        previewing = 3,
        accepted = 4,
        dismissed = 5,
        unavailable = 6 // no suggestion, with a reason the user can act on
    };

    enum class Reason
    {
        none = 0,
        noRhythm = 1,
        tempoUncertain = 2,
        tempoMismatch = 3,
        failed = 4,
        drumsPresent = 5,
        offGrid = 6
    };

    struct Snapshot
    {
        State state = State::idle;
        Reason reason = Reason::none;
        std::string message;
        std::string grooveName;
        std::string detail; // why this groove, in a few words
        model::ClipId clip;
        bool canTryAnother = false;
    };

    AccompanimentService (Session& session, SampleLoader& samples, engine::Engine& engine);
    ~AccompanimentService();

    // Looks at an audio clip and suggests a beat for it (a take just recorded, or any audio clip).
    void suggestFor (model::ClipId clip);
    void preview();
    void stopPreview();
    void add();
    void next();
    void dismiss();

    // The host calls these when the project changed or clip audio finished loading.
    void projectChanged();
    void audioChanged();

    [[nodiscard]] const Snapshot& snapshot() const noexcept { return current; }

    std::function<void()> onChanged;
    // Product events for local measurement (ADR-011 §16): suggested, previewed, skipped, changed,
    // accepted, removed. Names only; never audio or file names.
    std::function<void (std::string_view)> onEvent;

    static constexpr int analysisVersion = analysis::RhythmAnalysis::version;

private:
    struct Token;

    void begin (model::ClipId clip);
    void startAnalysisIfReady();
    void finish (const analysis::RhythmAnalysis& result);
    void setUnavailable (Reason reason, std::string message);
    void offer (std::size_t index);
    void setState (State state);
    [[nodiscard]] std::optional<model::Project> projectWithGroove (const accompaniment::Groove& groove) const;
    void publish();
    void event (std::string_view name);

    Session& session;
    SampleLoader& samples;
    engine::Engine& engine;

    Snapshot current;
    std::uint64_t generation = 0;
    bool waitingForAudio = false;
    bool adding = false;
    model::ClipId addedClip;

    accompaniment::MusicalContext context;
    std::vector<accompaniment::Suggestion> ranked;
    std::string offered;            // the groove id on offer
    std::vector<std::string> shown; // grooves already offered for this take

    std::map<std::uint64_t, std::pair<int, analysis::RhythmAnalysis>> cache; // asset -> (version, result)

    std::shared_ptr<Token> token;
    juce::ThreadPool pool {
        juce::ThreadPoolOptions {}.withThreadName ("Rhythm analysis").withNumberOfThreads (1)};
};

} // namespace ap::desktop
