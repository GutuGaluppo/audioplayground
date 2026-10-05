#pragma once

#include "ap/model/Project.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

namespace ap::model
{

// Project file format v1 (ADR-006). project.json is untrusted input: anything that is not
// exactly a valid v1 project is rejected with a reason; nothing is guessed or silently dropped.

inline constexpr int currentSchemaVersion = 1;
inline constexpr std::size_t maxProjectFileBytes = 16 * 1024 * 1024;
inline constexpr int maxJsonNestingDepth = 32;
inline constexpr std::size_t maxTimestampLength = 40;

struct ProjectMetadata
{
    std::string createdAt; // ISO 8601 UTC, e.g. "2026-10-05T19:00:00Z"
    std::string updatedAt;

    bool operator== (const ProjectMetadata&) const = default;
};

struct LoadedProject
{
    Project project;
    ProjectMetadata metadata;
};

struct LoadError
{
    enum class Code
    {
        tooLarge,
        tooDeeplyNested,
        notJson,
        newerVersion, // written by a newer app: refuse rather than lose data
        invalid
    };

    Code code = Code::invalid;
    std::string message; // safe to show to the user; never contains file contents

    bool operator== (const LoadError&) const = default;
};

using LoadResult = std::variant<LoadedProject, LoadError>;

[[nodiscard]] std::string serialiseProject (const Project& project, const ProjectMetadata& metadata);
[[nodiscard]] LoadResult parseProject (std::string_view json);

// Current time as ISO 8601 UTC with second precision.
[[nodiscard]] std::string currentTimestampUtc();

} // namespace ap::model
