// Proves RealtimeSanitizer is armed: allocating inside a nonblocking function must abort.
// Registered with WILL_FAIL, so the test passes only when RTSan catches the violation.
// Built without -Wfunction-effects, which would reject this code at compile time.

#include <memory>

namespace
{
[[clang::nonblocking]] int allocateOnAudioThread() noexcept
{
    auto value = std::make_unique<int> (42);
    return *value;
}
} // namespace

int main()
{
    return allocateOnAudioThread() == 42 ? 0 : 1;
}
