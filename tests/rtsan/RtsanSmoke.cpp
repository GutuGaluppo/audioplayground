// Proves RealtimeSanitizer is armed: allocating inside a nonblocking function must abort.
// The test passes only if RTSan's report appears in the output (PASS_REGULAR_EXPRESSION).
// Built without -Wfunction-effects, which would reject this code at compile time.

#include <memory>

namespace
{
int allocateOnAudioThread() noexcept [[clang::nonblocking]]
{
    auto value = std::make_unique<int> (42);
    return *value;
}
} // namespace

int main()
{
    return allocateOnAudioThread() == 42 ? 0 : 1;
}
