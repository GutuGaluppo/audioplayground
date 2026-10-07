#include "AccompanimentService.h"

#include "ap/accompaniment/MusicalContext.h"
#include "ap/model/ProjectDocument.h"

#include <algorithm>
#include <cmath>

namespace ap::desktop
{
namespace
{
using Reason = AccompanimentService::Reason;

std::string bpmText (double bpm)
{
    return std::to_string (static_cast<int> (std::lround (bpm)));
}

double ticksToSeconds (core::Ticks ticks, double bpm)
{
    return static_cast<double> (ticks) * 60.0 / (bpm * static_cast<double> (core::ticksPerQuarterNote));
}

std::string join (const std::vector<std::string>& parts)
{
    std::string text;
    for (const auto& part : parts)
        text += (text.empty() ? "" : " \xC2\xB7 ") + part; // " · "
    return text;
}
} // namespace

// Lets a worker's late result find out that the service is gone or has moved on.
struct AccompanimentService::Token
{
    std::atomic<bool> cancel {false};
};

AccompanimentService::AccompanimentService (Session& sessionToUse, SampleLoader& samplesToUse,
                                            engine::Engine& engineToUse)
    : session (sessionToUse)
    , samples (samplesToUse)
    , engine (engineToUse)
    , token (std::make_shared<Token>())
{
}

AccompanimentService::~AccompanimentService()
{
    token->cancel = true;
    pool.removeAllJobs (true, 5000);
}

void AccompanimentService::event (std::string_view name)
{
    if (onEvent)
        onEvent (name);
}

void AccompanimentService::setState (State state)
{
    current.state = state;
    if (onChanged)
        onChanged();
}

void AccompanimentService::setUnavailable (Reason reason, std::string message)
{
    ranked.clear();
    offered.clear();
    current.reason = reason;
    current.message = std::move (message);
    current.grooveName.clear();
    current.detail.clear();
    current.canTryAnother = false;
    setState (State::unavailable);
}

void AccompanimentService::suggestFor (model::ClipId clip)
{
    const auto location = session.project().locate (clip);
    if (!location || !session.project().tracks[location->track].clips[location->clip].asset.isValid())
        return; // only audio clips can be listened to
    begin (clip);
}

void AccompanimentService::begin (model::ClipId clip)
{
    if (session.isPreviewing())
        stopPreview();
    ++generation;
    token->cancel = true; // a running analysis is for the previous take
    token = std::make_shared<Token>();

    ranked.clear();
    shown.clear();
    offered.clear();
    addedClip = {};
    current = {};
    current.clip = clip;
    current.message = "Listening to your take\xE2\x80\xA6";
    waitingForAudio = true;
    setState (State::analyzing);
    startAnalysisIfReady();
}

void AccompanimentService::audioChanged()
{
    if (waitingForAudio)
        startAnalysisIfReady();
}

void AccompanimentService::startAnalysisIfReady()
{
    const auto location = session.project().locate (current.clip);
    if (!location)
    {
        waitingForAudio = false;
        current = {};
        setState (State::idle);
        return;
    }
    const auto asset = session.project().tracks[location->track].clips[location->clip].asset;

    if (const auto cached = cache.find (asset.value);
        cached != cache.end() && cached->second.first == analysisVersion)
    {
        waitingForAudio = false;
        finish (cached->second.second);
        return;
    }

    const auto& audio = samples.getClipAudio();
    const auto found = audio.find (asset.value);
    if (found == audio.end() || found->second.state.loading || !found->second.buffer)
    {
        if (found != audio.end() && found->second.state.missing)
        {
            waitingForAudio = false;
            setUnavailable (Reason::failed, "Analysis failed: the audio file is missing.");
        }
        return; // still decoding: audioChanged() will call again
    }

    waitingForAudio = false;
    const auto buffer = found->second.buffer;
    const auto expectedGeneration = generation;
    const auto weak = std::weak_ptr<Token> (token);
    const auto flag = token;
    pool.addJob (
        [this, buffer, asset, expectedGeneration, weak, flag]
        {
            auto result = analysis::analyseRhythm (*buffer, {}, &flag->cancel);
            const bool cancelled = flag->cancel.load();
            juce::MessageManager::callAsync (
                [this, result = std::move (result), asset, expectedGeneration, weak, cancelled]
                {
                    const auto alive = weak.lock();
                    if (!alive || cancelled || expectedGeneration != generation)
                        return;
                    if (result.durationSeconds <= 0.0)
                    {
                        setUnavailable (Reason::failed, "Analysis failed, try again later.");
                        return;
                    }
                    cache[asset.value] = {analysisVersion, result};
                    finish (result);
                });
        });
}

void AccompanimentService::finish (const analysis::RhythmAnalysis& result)
{
    const auto location = session.project().locate (current.clip);
    if (!location)
    {
        current = {};
        setState (State::idle);
        return;
    }
    const auto& project = session.project();
    const auto& clip = project.tracks[location->track].clips[location->clip];
    const double bpm = project.tempoBpm;

    // Where the take's first beat falls in the project: its position, plus the beat's time inside
    // the file (the clip may start part-way into it).
    std::optional<double> firstBeat;
    const double sourceStart
        = static_cast<double> (clip.sourceOffset) / static_cast<double> (core::flicksPerSecond);
    for (const double beat : result.beatSeconds)
        if (beat >= sourceStart)
        {
            firstBeat = ticksToSeconds (clip.start, bpm) + (beat - sourceStart);
            break;
        }

    const auto verdict = accompaniment::makeContext (result, bpm, project.timeSignature, firstBeat);
    switch (verdict.verdict)
    {
    case accompaniment::Verdict::noRhythm:
        setUnavailable (Reason::noRhythm,
                        "Couldn't find a rhythm pattern here. Try tapping the tempo or add drums manually.");
        return;
    case accompaniment::Verdict::tempoUncertain:
        setUnavailable (Reason::tempoUncertain, "Not sure about the tempo (about "
                                                    + bpmText (verdict.detectedBpm)
                                                    + " BPM). Set the project tempo, then try again.");
        return;
    case accompaniment::Verdict::tempoMismatch:
        setUnavailable (Reason::tempoMismatch, "This sounds like about " + bpmText (verdict.detectedBpm)
                                                   + " BPM, but the project is at " + bpmText (bpm)
                                                   + ". Change the project tempo to match, then try again.");
        return;
    case accompaniment::Verdict::offGrid:
        setUnavailable (Reason::offGrid,
                        "This take isn't in time with the project's beat. Record along with the metronome, "
                        "then try again.");
        return;
    case accompaniment::Verdict::ready:
        break;
    }

    // Drums already playing here: a second beat on top would be noise.
    const auto bar = project.timeSignature.ticksPerBar();
    const auto start = clip.start / bar * bar;
    if (const auto* drums = project.findInstrumentTrack (model::InstrumentKind::drums))
        for (const auto& other : drums->clips)
            if (other.start < clip.end() && start < other.end())
            {
                setUnavailable (Reason::drumsPresent, "There are already drums here.");
                return;
            }

    context = verdict.context;
    ranked = accompaniment::findAccompaniment (accompaniment::Kind::drums, context,
                                               accompaniment::factoryGrooves());
    if (ranked.empty())
    {
        setUnavailable (Reason::none, "No beat in the library fits this one yet.");
        return;
    }
    offer (0);
    event ("accompaniment_suggested");
}

void AccompanimentService::offer (std::size_t index)
{
    const auto& suggestion = ranked[index];
    const auto* groove = accompaniment::factoryGrooves().find (suggestion.grooveId);
    offered = suggestion.grooveId;
    shown.push_back (offered);

    // Is there another one to try after this?
    auto exclude = shown;
    current.canTryAnother = !accompaniment::findAccompaniment (accompaniment::Kind::drums, context,
                                                               accompaniment::factoryGrooves(), exclude)
                                 .empty();
    current.reason = Reason::none;
    current.message = "Try a beat";
    current.grooveName = groove != nullptr ? groove->name : suggestion.grooveId;
    current.detail = join (suggestion.reasons);
    setState (current.state == State::previewing ? State::previewing : State::suggestionReady);
}

std::optional<model::Project>
AccompanimentService::projectWithGroove (const accompaniment::Groove& groove) const
{
    const auto& project = session.project();
    const auto location = project.locate (current.clip);
    if (!location)
        return std::nullopt;
    const auto& take = project.tracks[location->track].clips[location->clip];

    const auto bar = project.timeSignature.ticksPerBar();
    const auto start = take.start / bar * bar;
    const auto end = (take.end() + bar - 1) / bar * bar;

    model::ProjectDocument copy (project);
    model::TrackId track;
    if (const auto* drums = copy.project().findInstrumentTrack (model::InstrumentKind::drums))
        track = drums->id;
    else if (!copy.perform (model::AddTrack {model::InstrumentKind::drums}))
        return std::nullopt;
    else
        track = copy.project().findInstrumentTrack (model::InstrumentKind::drums)->id;

    if (!copy.perform (
            model::AddClip {track, accompaniment::grooveClip (groove, start, std::max (bar, end - start))}))
        return std::nullopt;
    return copy.project();
}

void AccompanimentService::publish()
{
    const auto* groove = accompaniment::factoryGrooves().find (offered);
    if (groove == nullptr)
        return;
    if (auto hypothetical = projectWithGroove (*groove))
        session.setPreview (std::move (*hypothetical));
}

void AccompanimentService::preview()
{
    if (current.state != State::suggestionReady && current.state != State::previewing)
        return;
    const auto* groove = accompaniment::factoryGrooves().find (offered);
    const auto location = session.project().locate (current.clip);
    if (groove == nullptr || !location)
        return;

    publish();
    if (!session.isPreviewing())
        return;
    const auto bar = session.project().timeSignature.ticksPerBar();
    const auto& take = session.project().tracks[location->track].clips[location->clip];
    engine.getTransport().requestSeek (take.start / bar * bar);
    engine.getTransport().requestPlay();
    const bool first = current.state != State::previewing;
    setState (State::previewing);
    if (first)
        event ("accompaniment_previewed");
}

void AccompanimentService::stopPreview()
{
    if (current.state != State::previewing)
        return;
    engine.getTransport().requestStop();
    session.endPreview();
    setState (State::suggestionReady);
}

void AccompanimentService::next()
{
    if (current.state != State::suggestionReady && current.state != State::previewing)
        return;
    auto fresh = accompaniment::findAccompaniment (accompaniment::Kind::drums, context,
                                                   accompaniment::factoryGrooves(), shown);
    if (fresh.empty())
        return;
    ranked = std::move (fresh);
    const bool wasPreviewing = current.state == State::previewing;
    offer (0);
    event ("accompaniment_changed");
    if (wasPreviewing)
        publish(); // the next groove replaces the one playing; the transport keeps going
}

void AccompanimentService::dismiss()
{
    if (current.state == State::suggestionReady || current.state == State::previewing)
        event ("accompaniment_skipped");
    if (current.state == State::previewing)
    {
        engine.getTransport().requestStop();
        session.endPreview();
    }
    ++generation;
    token->cancel = true;
    token = std::make_shared<Token>();
    waitingForAudio = false;
    ranked.clear();
    offered.clear();
    current.canTryAnother = false;
    current.message.clear();
    current.grooveName.clear();
    current.detail.clear();
    current.reason = Reason::none;
    setState (State::dismissed);
}

void AccompanimentService::add()
{
    if (current.state != State::suggestionReady && current.state != State::previewing)
        return;
    const auto* groove = accompaniment::factoryGrooves().find (offered);
    const auto location = session.project().locate (current.clip);
    if (groove == nullptr || !location)
        return;

    const auto& project = session.project();
    const auto& take = project.tracks[location->track].clips[location->clip];
    const auto bar = project.timeSignature.ticksPerBar();
    const auto start = take.start / bar * bar;
    const auto end = (take.end() + bar - 1) / bar * bar;
    auto clip = accompaniment::grooveClip (*groove, start, std::max (bar, end - start));

    model::ClipId created;
    adding = true;
    const bool ok = session.performGroup (
        "Add drums",
        [&] (model::ProjectDocument::Group& group)
        {
            model::TrackId track;
            if (const auto* drums = group.project().findInstrumentTrack (model::InstrumentKind::drums))
                track = drums->id;
            else if (const auto* added = group.perform (model::AddTrack {model::InstrumentKind::drums}))
                track = std::get<model::AddTrack> (*added).created;
            if (const auto* result = group.perform (model::AddClip {track, std::move (clip)}))
                created = std::get<model::AddClip> (*result).created;
        });
    adding = false;
    if (!ok || !created.isValid())
        return;

    addedClip = created;
    current.message = "Added " + current.grooveName;
    current.canTryAnother = false;
    event ("accompaniment_accepted");
    setState (State::accepted);
}

void AccompanimentService::projectChanged()
{
    if (adding)
        return;

    if (current.state == State::analyzing || current.state == State::suggestionReady
        || current.state == State::previewing)
    {
        if (!session.project().locate (current.clip))
        {
            // The take is gone (deleted, undone, another project): nothing to suggest for.
            ++generation;
            token->cancel = true;
            token = std::make_shared<Token>();
            waitingForAudio = false;
            ranked.clear();
            offered.clear();
            current = {};
            setState (State::idle);
            return;
        }
    }
    if (current.state == State::previewing && !session.isPreviewing())
        setState (State::suggestionReady); // an edit ended the preview
    if (current.state == State::accepted && addedClip.isValid() && !session.project().locate (addedClip))
    {
        // The beat was undone: the suggestion is still good, offer it again.
        addedClip = {};
        event ("accompaniment_removed");
        current.message = "Try a beat";
        current.canTryAnother = true;
        setState (State::suggestionReady);
    }
}

} // namespace ap::desktop
