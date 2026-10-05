#pragma once

#include "ap/model/ProjectSerialization.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ap::io
{

namespace fs = std::filesystem;

// On-disk layout of a project folder (ADR-006):
//
//   MySong.playground/
//   ├── project.json      current project (written atomically)
//   ├── project.json.bak  previous save
//   ├── audio/            assets referenced by the project
//   ├── cache/            disposable
//   └── autosave/         rotating autosaves
struct ProjectFolder
{
    fs::path root;

    [[nodiscard]] fs::path projectFile() const { return root / "project.json"; }
    [[nodiscard]] fs::path backupFile() const { return root / "project.json.bak"; }
    [[nodiscard]] fs::path audioDirectory() const { return root / "audio"; }
    [[nodiscard]] fs::path cacheDirectory() const { return root / "cache"; }
    [[nodiscard]] fs::path autosaveDirectory() const { return root / "autosave"; }
};

inline constexpr std::string_view projectFolderExtension = ".playground";
inline constexpr std::size_t maxAutosaves = 5;

struct IoError
{
    std::string message; // safe to show to the user

    bool operator== (const IoError&) const = default;
};

// Writes contents to path so that readers see either the old file or the complete new one,
// never a partial file: write to a temporary file in the same folder, flush to disk, rename.
[[nodiscard]] std::optional<IoError> writeFileAtomically (const fs::path& path, std::string_view contents);

// Reads at most maxBytes; larger files are refused rather than truncated.
[[nodiscard]] std::variant<std::string, IoError> readFileLimited (const fs::path& path, std::size_t maxBytes);

// Creates the folder structure if needed, keeps the previous project.json as a backup and
// writes the new one atomically.
[[nodiscard]] std::optional<IoError> saveProject (const ProjectFolder& folder, const model::Project& project,
                                                  const model::ProjectMetadata& metadata);

using LoadFolderResult = std::variant<model::LoadedProject, model::LoadError>;

// Loads project.json. Never modifies anything on disk.
[[nodiscard]] LoadFolderResult loadProject (const ProjectFolder& folder);
[[nodiscard]] LoadFolderResult loadBackup (const ProjectFolder& folder);

// Writes a new autosave and deletes the oldest beyond maxAutosaves. Never touches project.json.
[[nodiscard]] std::optional<IoError> writeAutosave (const ProjectFolder& folder,
                                                    const model::Project& project,
                                                    const model::ProjectMetadata& metadata);

// Autosave files, newest first.
[[nodiscard]] std::vector<fs::path> listAutosaves (const ProjectFolder& folder);

// Maps a project-relative asset reference ("audio/take-1.wav") to a path that is guaranteed to
// stay inside the project folder. Rejects absolute paths, "..", empty or odd components and
// symbolic links that escape the folder. Returns nullopt when unsafe.
[[nodiscard]] std::optional<fs::path> resolveAssetPath (const ProjectFolder& folder,
                                                        std::string_view relative);

} // namespace ap::io
