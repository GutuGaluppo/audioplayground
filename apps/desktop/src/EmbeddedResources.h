#pragma once

#include <cstddef>
#include <span>

namespace ap::desktop::ui
{

struct EmbeddedFile
{
    const char* path; // relative to the UI bundle root, '/'-separated
    const unsigned char* data;
    std::size_t size;
};

// The built UI bundle (ui/dist), embedded at build time by cmake/EmbedDirectory.cmake.
[[nodiscard]] std::span<const EmbeddedFile> embeddedFiles() noexcept;

} // namespace ap::desktop::ui
