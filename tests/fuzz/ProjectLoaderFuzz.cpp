// libFuzzer target for the project loader (ADR-006: project files are untrusted input).
//
// Properties checked for every input:
// 1. parseProject never crashes, hangs or triggers sanitizer reports.
// 2. Anything accepted round-trips: serialise -> parse gives the same project and metadata.

#include "ap/model/ProjectSerialization.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <variant>

extern "C" int LLVMFuzzerTestOneInput (const std::uint8_t* data, std::size_t size)
{
    const std::string_view text (reinterpret_cast<const char*> (data), size);
    const auto result = ap::model::parseProject (text);

    if (const auto* loaded = std::get_if<ap::model::LoadedProject> (&result))
    {
        const auto again
            = ap::model::parseProject (ap::model::serialiseProject (loaded->project, loaded->metadata));
        const auto* reloaded = std::get_if<ap::model::LoadedProject> (&again);
        if (reloaded == nullptr || !(reloaded->project == loaded->project)
            || !(reloaded->metadata == loaded->metadata))
            std::abort();
    }
    return 0;
}
