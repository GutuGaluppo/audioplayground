#pragma once

// Internal helpers shared by the io sources: wide-path file opening on Windows and durable flush.

#include <cstdio>
#include <filesystem>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace ap::io::detail
{

// mode: "rb", "wb" or "r+b".
inline std::FILE* openFile (const std::filesystem::path& path, const char* mode)
{
#if defined(_WIN32)
    wchar_t wideMode[4] {};
    for (int i = 0; i < 3 && mode[i] != '\0'; ++i)
        wideMode[i] = static_cast<wchar_t> (mode[i]);
    std::FILE* file = nullptr;
    return _wfopen_s (&file, path.c_str(), wideMode) == 0 ? file : nullptr;
#else
    return std::fopen (path.c_str(), mode);
#endif
}

// Flushes the C buffer and asks the OS to put the data on the disk.
inline bool flushToDisk (std::FILE* file)
{
    if (std::fflush (file) != 0)
        return false;
#if defined(_WIN32)
    return _commit (_fileno (file)) == 0;
#else
    return fsync (fileno (file)) == 0;
#endif
}

} // namespace ap::io::detail
