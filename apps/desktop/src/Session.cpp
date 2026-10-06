#include "Session.h"

#include "ap/engine/Export.h"

#include <filesystem>
#include <system_error>

namespace ap::desktop
{
namespace
{
constexpr int timerHz = 10;
constexpr int autosaveIntervalTicks = 30 * timerHz; // every 30 s while there are changes

std::filesystem::file_time_type modificationTime (const std::filesystem::path& path)
{
    std::error_code ec;
    const auto time = std::filesystem::last_write_time (path, ec);
    return ec ? std::filesystem::file_time_type::min() : time;
}
} // namespace

Session::Session (engine::Engine& engineToUse, io::ProjectFolder scratchFolder)
    : engine (engineToUse)
    , scratch (std::move (scratchFolder))
{
    metadata.createdAt = model::currentTimestampUtc();
    metadata.updatedAt = metadata.createdAt;
    savedVersion = autosavedVersion = doc.version();
    syncEngine();
    startTimerHz (timerHz);
}

Session::~Session()
{
    stopTimer();
    engine.collectGarbage();
}

bool Session::perform (model::Command command, model::ProjectDocument::GestureId gesture)
{
    return afterChange (doc.perform (std::move (command), gesture));
}

bool Session::performGroup (std::string_view description,
                            const std::function<void (model::ProjectDocument::Group&)>& edit,
                            model::ProjectDocument::GestureId gesture)
{
    return afterChange (doc.performGroup (description, edit, gesture));
}

void Session::setAudioLookup (engine::AudioLookup lookup)
{
    audioLookup = std::move (lookup);
    refreshEngine();
}

void Session::refreshEngine()
{
    engine.publishRenderGraph (std::make_unique<engine::RenderGraph> (
        engine::buildRenderGraph (doc.project(), doc.version(), audioLookup)));
}

bool Session::undo()
{
    return afterChange (doc.undo());
}

bool Session::redo()
{
    return afterChange (doc.redo());
}

bool Session::afterChange (bool changed)
{
    if (changed)
    {
        syncEngine();
        notify();
    }
    return changed;
}

void Session::notify()
{
    if (onChanged)
        onChanged();
}

void Session::syncEngine()
{
    engine::configureEngine (engine, doc.project(), doc.version(), audioLookup);
}

void Session::loadInto (model::LoadedProject loaded, std::optional<io::ProjectFolder> location,
                        bool markDirty)
{
    doc.reset (std::move (loaded.project));
    metadata = std::move (loaded.metadata);
    folder = std::move (location);
    savedVersion = markDirty ? 0 : doc.version();
    autosavedVersion = doc.version();
    syncEngine();
    notify();
}

void Session::newProject (model::Project initial)
{
    model::LoadedProject fresh;
    fresh.project = std::move (initial);
    fresh.metadata.createdAt = model::currentTimestampUtc();
    fresh.metadata.updatedAt = fresh.metadata.createdAt;
    loadInto (std::move (fresh), std::nullopt, false);
}

std::optional<std::string> Session::save()
{
    if (!folder)
        return "Choose where to save the project first.";
    return saveAs (*folder);
}

std::optional<std::string> Session::saveAs (const io::ProjectFolder& target)
{
    // Bring the audio files along when the project moves (first save, or Save As elsewhere).
    const auto& source = assetRoot();
    std::error_code ec;
    if (!std::filesystem::equivalent (source.root, target.root, ec))
    {
        std::filesystem::create_directories (target.audioDirectory(), ec);
        for (const auto& asset : doc.project().assets)
        {
            const auto from = io::resolveAssetPath (source, asset.relativePath);
            if (!from || !std::filesystem::exists (*from))
                continue; // missing files stay missing; the reference is kept
            const auto to = target.root / std::filesystem::path (asset.relativePath);
            std::filesystem::copy_file (*from, to, std::filesystem::copy_options::skip_existing, ec);
            if (ec)
                return "Could not copy the project's audio files.";
        }
    }

    auto updated = metadata;
    updated.updatedAt = model::currentTimestampUtc();

    if (const auto error = io::saveProject (target, doc.project(), updated))
        return error->message;

    const bool wasUntitled = !folder.has_value();
    metadata = std::move (updated);
    folder = target;
    savedVersion = autosavedVersion = doc.version();
    if (wasUntitled)
        discardScratchAutosaves();
    notify();
    return std::nullopt;
}

std::optional<std::string> Session::open (const io::ProjectFolder& source)
{
    auto result = io::loadProject (source);
    if (auto* error = std::get_if<model::LoadError> (&result))
        return error->message;

    loadInto (std::move (std::get<model::LoadedProject> (result)), source, false);
    return std::nullopt;
}

std::optional<std::string> Session::recoverFromAutosave (std::optional<io::ProjectFolder> lastProject)
{
    const auto& candidateFolder = lastProject ? *lastProject : scratch;
    const auto autosaves = io::listAutosaves (candidateFolder);
    if (autosaves.empty())
        return std::nullopt;

    // An autosave older than the last explicit save holds nothing new.
    if (lastProject && modificationTime (autosaves.front()) <= modificationTime (lastProject->projectFile()))
        return std::nullopt;

    auto text = io::readFileLimited (autosaves.front(), model::maxProjectFileBytes);
    if (!std::holds_alternative<std::string> (text))
        return std::nullopt;

    auto parsed = model::parseProject (std::get<std::string> (text));
    if (!std::holds_alternative<model::LoadedProject> (parsed))
        return std::nullopt;

    loadInto (std::move (std::get<model::LoadedProject> (parsed)), lastProject, true);
    return lastProject ? std::string ("Recovered unsaved changes to \"") + doc.project().name + "\"."
                       : std::string ("Recovered your unsaved work from the last session.");
}

void Session::discardScratchAutosaves()
{
    std::error_code ec;
    for (const auto& file : io::listAutosaves (scratch))
        std::filesystem::remove (file, ec);
    std::filesystem::remove_all (scratch.audioDirectory(), ec);
}

void Session::autosaveIfNeeded()
{
    if (doc.version() == autosavedVersion || !isDirty())
        return;

    const auto& target = folder ? *folder : scratch;
    if (!io::writeAutosave (target, doc.project(), metadata))
        autosavedVersion = doc.version();
}

void Session::timerCallback()
{
    // Frees render graphs the audio thread has retired (never freed on the audio thread).
    engine.collectGarbage();

    if (++ticksSinceAutosave >= autosaveIntervalTicks)
    {
        ticksSinceAutosave = 0;
        autosaveIfNeeded();
    }
}

} // namespace ap::desktop
