#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

namespace ap::test
{

// A fresh directory under the system temp folder, removed (recursively) on destruction.
class TempDirectory
{
public:
    TempDirectory()
    {
        static std::atomic<int> counter {0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = std::filesystem::temp_directory_path()
            / ("ap-test-" + std::to_string (stamp) + "-" + std::to_string (counter++));
        std::filesystem::create_directories (dir);
    }

    ~TempDirectory()
    {
        std::error_code ec;
        std::filesystem::remove_all (dir, ec);
    }

    TempDirectory (const TempDirectory&) = delete;
    TempDirectory& operator= (const TempDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return dir; }

private:
    std::filesystem::path dir;
};

} // namespace ap::test
