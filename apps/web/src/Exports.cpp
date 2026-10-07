// The browser host's C interface: what the AudioWorklet calls into the WebAssembly module. One
// host per module. No JavaScript glue is needed (the module is standalone); strings pass through
// memory the module hands out (ap_alloc).

#include "WebHost.h"

#include <cstdlib>
#include <memory>

namespace
{
std::unique_ptr<ap::web::WebHost> host;
std::string lastEvents;
std::string projectText;
std::string projectError;
} // namespace

#define AP_EXPORT extern "C" __attribute__ ((used, visibility ("default")))

AP_EXPORT void* ap_alloc (int bytes)
{
    return std::malloc (static_cast<std::size_t> (std::max (1, bytes)));
}

AP_EXPORT void ap_free (void* pointer)
{
    std::free (pointer);
}

// Creates (or replaces) the host. Returns 1 on success.
AP_EXPORT int ap_create (double sampleRate, int blockSize)
{
    if (!(sampleRate >= 8000.0 && sampleRate <= 768000.0) || blockSize < 1 || blockSize > 8192)
        return 0;
    host = std::make_unique<ap::web::WebHost> (sampleRate, blockSize);
    return 1;
}

// A UI message as UTF-8 JSON. Returns 1 if it was understood.
AP_EXPORT int ap_intent (const char* json, int length)
{
    if (!host || json == nullptr || length < 0)
        return 0;
    return host->handleIntent (std::string_view (json, static_cast<std::size_t> (length))) ? 1 : 0;
}

// Renders `frames` frames into two channel buffers owned by the caller.
AP_EXPORT void ap_process (float* left, float* right, int frames)
{
    if (host && left != nullptr && right != nullptr && frames > 0)
        host->process (left, right, frames);
}

// The events since the last call as a JSON array; read ap_events_size() bytes from the pointer.
AP_EXPORT const char* ap_events()
{
    lastEvents = host ? host->takeEvents() : std::string ("[]");
    return lastEvents.c_str();
}

AP_EXPORT int ap_events_size()
{
    return static_cast<int> (lastEvents.size());
}

// Saving and opening. The text of an exported project stays valid until the next export; an import
// error likewise. A page that is not allowed to send more than ap_max_project_bytes gets 0.
AP_EXPORT int ap_project_export (const char* timestamp, int length)
{
    if (!host || timestamp == nullptr || length < 0)
        return -1;
    projectText = host->exportProject (std::string_view (timestamp, static_cast<std::size_t> (length)));
    return static_cast<int> (projectText.size());
}

AP_EXPORT const char* ap_project_text()
{
    return projectText.c_str();
}

AP_EXPORT int ap_project_import (const char* json, int length, int dirty, int hasLocation)
{
    if (!host || json == nullptr || length < 0)
        return 0;
    projectError = host->importProject (std::string_view (json, static_cast<std::size_t> (length)),
                                        dirty != 0, hasLocation != 0);
    return projectError.empty() ? 1 : 0;
}

AP_EXPORT const char* ap_project_error()
{
    return projectError.c_str();
}

AP_EXPORT int ap_project_error_size()
{
    return static_cast<int> (projectError.size());
}

AP_EXPORT void ap_project_saved (int hasLocation)
{
    if (host)
        host->projectSaved (hasLocation != 0);
}

AP_EXPORT void ap_project_unsaved()
{
    if (host)
        host->projectUnsaved();
}

AP_EXPORT void ap_set_output_latency (double milliseconds)
{
    if (host)
        host->setOutputLatency (milliseconds);
}
