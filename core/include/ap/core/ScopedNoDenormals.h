#pragma once

#include "ap/core/RealtimeSafety.h"

#include <cstdint>

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#endif

namespace ap::core
{

// Enables flush-to-zero (and denormals-are-zero on x86) for the current scope.
// Denormal floats in feedback paths (filters, reverbs) can cost 100x CPU per sample.
class ScopedNoDenormals
{
public:
    ScopedNoDenormals() noexcept AP_NONBLOCKING
    {
#if defined(__aarch64__)
        std::uint64_t fpcr;
        asm volatile ("mrs %0, fpcr" : "=r"(fpcr));
        previous = fpcr;
        fpcr |= flushToZeroBit;
        asm volatile ("msr fpcr, %0" : : "r"(fpcr));
#elif defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
        previous = _mm_getcsr();
        _mm_setcsr (static_cast<unsigned int> (previous) | flushToZeroAndDenormalsAreZero);
#endif
    }

    ~ScopedNoDenormals() AP_NONBLOCKING
    {
#if defined(__aarch64__)
        asm volatile ("msr fpcr, %0" : : "r"(previous));
#elif defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
        _mm_setcsr (static_cast<unsigned int> (previous));
#endif
    }

    ScopedNoDenormals (const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator= (const ScopedNoDenormals&) = delete;

private:
#if defined(__aarch64__)
    static constexpr std::uint64_t flushToZeroBit = std::uint64_t{1} << 24;
#elif defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
    static constexpr unsigned int flushToZeroAndDenormalsAreZero = 0x8040u;
#endif
    [[maybe_unused]] std::uint64_t previous = 0;
};

} // namespace ap::core
