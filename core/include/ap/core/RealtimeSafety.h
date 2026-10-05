#pragma once

// AP_NONBLOCKING marks functions that run on the real-time audio thread.
// On Clang it expands to [[clang::nonblocking]]; with -Wfunction-effects (enabled in
// cmake/ProjectOptions.cmake) the compiler rejects allocation, locks, exceptions and calls to
// functions not known to be non-blocking. RealtimeSanitizer checks the same contract at run time.
#if defined(__clang__) && defined(__has_cpp_attribute)
#if __has_cpp_attribute(clang::nonblocking)
#define AP_NONBLOCKING [[clang::nonblocking]]
#endif
#endif

#ifndef AP_NONBLOCKING
#define AP_NONBLOCKING
#endif
