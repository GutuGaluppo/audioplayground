#include "ap/io/ProjectFiles.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <system_error>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace ap::io
{
namespace
{
std::string temporarySuffix()
{
    // Unique within the process; the folder is per-project so cross-process races are not a concern.
    static std::atomic<unsigned> counter {0};
    return ".tmp-" + std::to_string (counter.fetch_add (1));
}

bool flushToDisk (std::FILE* file)
{
    if (std::fflush (file) != 0)
        return false;
#if defined(_WIN32)
    return _commit (_fileno (file)) == 0;
#else
    return fsync (fileno (file)) == 0;
#endif
}

std::FILE* openForWriting (const fs::path& path)
{
#if defined(_WIN32)
    std::FILE* file = nullptr;
    return _wfopen_s (&file, path.c_str(), L"wb") == 0 ? file : nullptr;
#else
    return std::fopen (path.c_str(), "wb");
#endif
}

std::FILE* openForReading (const fs::path& path)
{
#if defined(_WIN32)
    std::FILE* file = nullptr;
    return _wfopen_s (&file, path.c_str(), L"rb") == 0 ? file : nullptr;
#else
    return std::fopen (path.c_str(), "rb");
#endif
}

LoadFolderResult loadFile (const fs::path& path)
{
    auto contents = readFileLimited (path, model::maxProjectFileBytes);
    if (const auto* error = std::get_if<IoError> (&contents))
        return model::LoadError {model::LoadError::Code::invalid, error->message};
    return model::parseProject (std::get<std::string> (contents));
}

bool isPlainComponent (const fs::path& component)
{
    const auto text = component.u8string();
    if (text.empty() || text == u8"." || text == u8".." || text.size() > 255)
        return false;
    return std::none_of (text.begin(), text.end(),
                         [] (char8_t c)
                         {
                             return c < 0x20 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"'
                                 || c == '<' || c == '>' || c == '|';
                         });
}

bool isWithin (const fs::path& child, const fs::path& parent)
{
    const auto mismatch = std::mismatch (parent.begin(), parent.end(), child.begin(), child.end());
    return mismatch.first == parent.end();
}
} // namespace

std::optional<IoError> writeFileAtomically (const fs::path& path, std::string_view contents)
{
    auto temporary = path;
    temporary += temporarySuffix();

    std::FILE* file = openForWriting (temporary);
    if (file == nullptr)
        return IoError {"Could not write to the project folder. Check that it is not read-only."};

    const bool written = std::fwrite (contents.data(), 1, contents.size(), file) == contents.size();
    const bool flushed = written && flushToDisk (file);
    const bool closed = std::fclose (file) == 0;

    std::error_code ec;
    if (!(written && flushed && closed))
    {
        fs::remove (temporary, ec);
        return IoError {"Could not save. The disk may be full."};
    }

    fs::rename (temporary, path, ec); // atomic replace on POSIX; MoveFileEx(REPLACE_EXISTING) on Windows
    if (ec)
    {
        fs::remove (temporary, ec);
        return IoError {"Could not replace the project file."};
    }
    return std::nullopt;
}

std::variant<std::string, IoError> readFileLimited (const fs::path& path, std::size_t maxBytes)
{
    std::error_code ec;
    if (!fs::is_regular_file (path, ec))
        return IoError {"The project file was not found."};

    std::FILE* file = openForReading (path);
    if (file == nullptr)
        return IoError {"The project file could not be opened."};

    std::string contents;
    char buffer[64 * 1024];
    std::size_t read = 0;
    bool tooLarge = false;
    while ((read = std::fread (buffer, 1, sizeof (buffer), file)) > 0)
    {
        if (contents.size() + read > maxBytes)
        {
            tooLarge = true;
            break;
        }
        contents.append (buffer, read);
    }
    const bool failed = std::ferror (file) != 0;
    std::fclose (file);

    if (tooLarge)
        return IoError {"The project file is too large to open."};
    if (failed)
        return IoError {"The project file could not be read."};
    return contents;
}

std::optional<IoError> saveProject (const ProjectFolder& folder, const model::Project& project,
                                    const model::ProjectMetadata& metadata)
{
    std::error_code ec;
    for (const auto& directory :
         {folder.root, folder.audioDirectory(), folder.cacheDirectory(), folder.autosaveDirectory()})
    {
        fs::create_directories (directory, ec);
        if (ec)
            return IoError {"Could not create the project folder."};
    }

    // Keep the previous save. Copy (not rename) so project.json exists at every moment.
    if (fs::exists (folder.projectFile(), ec))
        fs::copy_file (folder.projectFile(), folder.backupFile(), fs::copy_options::overwrite_existing, ec);

    return writeFileAtomically (folder.projectFile(), model::serialiseProject (project, metadata));
}

LoadFolderResult loadProject (const ProjectFolder& folder)
{
    return loadFile (folder.projectFile());
}

LoadFolderResult loadBackup (const ProjectFolder& folder)
{
    return loadFile (folder.backupFile());
}

std::optional<IoError> writeAutosave (const ProjectFolder& folder, const model::Project& project,
                                      const model::ProjectMetadata& metadata)
{
    std::error_code ec;
    fs::create_directories (folder.autosaveDirectory(), ec);
    if (ec)
        return IoError {"Could not create the autosave folder."};

    // Sortable name: newest has the highest timestamp; the counter breaks ties within a second.
    static std::atomic<unsigned> sequence {0};
    auto stamp = model::currentTimestampUtc();
    std::replace (stamp.begin(), stamp.end(), ':', '-');
    char suffix[16] {};
    std::snprintf (suffix, sizeof (suffix), "-%04u", sequence.fetch_add (1) % 10000u);
    const auto path = folder.autosaveDirectory() / ("autosave-" + stamp + suffix + ".json");

    if (auto error = writeFileAtomically (path, model::serialiseProject (project, metadata)))
        return error;

    const auto autosaves = listAutosaves (folder);
    for (std::size_t i = maxAutosaves; i < autosaves.size(); ++i)
        fs::remove (autosaves[i], ec);

    return std::nullopt;
}

std::vector<fs::path> listAutosaves (const ProjectFolder& folder)
{
    std::vector<fs::path> files;
    std::error_code ec;
    for (fs::directory_iterator it (folder.autosaveDirectory(), ec), end; !ec && it != end; it.increment (ec))
    {
        const auto& path = it->path();
        const auto name = path.filename().string();
        if (it->is_regular_file (ec) && name.starts_with ("autosave-") && path.extension() == ".json")
            files.push_back (path);
    }
    std::sort (files.begin(), files.end(), [] (const fs::path& a, const fs::path& b)
               { return a.filename().string() > b.filename().string(); });
    return files;
}

std::optional<fs::path> resolveAssetPath (const ProjectFolder& folder, std::string_view relative)
{
    if (relative.empty() || relative.size() > 1024)
        return std::nullopt;

    // References are written by the app in one canonical form: '/'-separated plain names.
    // Anything else (absolute, "..", ".", empty components, backslashes, drive letters) is refused.
    fs::path requested;
    std::size_t start = 0;
    while (start <= relative.size())
    {
        const auto end = std::min (relative.find ('/', start), relative.size());
        const auto piece = relative.substr (start, end - start);
        const fs::path component (std::u8string (piece.begin(), piece.end()));
        if (!isPlainComponent (component))
            return std::nullopt;
        requested /= component;
        start = end + 1;
    }

    if (requested.is_absolute() || requested.has_root_name() || requested.has_root_directory())
        return std::nullopt;

    std::error_code ec;
    const auto root = fs::weakly_canonical (folder.root, ec);
    if (ec)
        return std::nullopt;

    // weakly_canonical resolves symbolic links for the parts that exist, so a link inside the
    // project that points elsewhere is caught here.
    const auto resolved = fs::weakly_canonical (root / requested, ec);
    if (ec || !isWithin (resolved, root) || resolved == root)
        return std::nullopt;

    return resolved;
}

} // namespace ap::io
