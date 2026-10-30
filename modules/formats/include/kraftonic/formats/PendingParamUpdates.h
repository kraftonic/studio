// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic plugin — background-thread parameter transfer.
//
//  The VSTGUI editor must only ever be touched on the UI thread: a parameter
//  change notification (`Parameter::setNormalized` -> `changed()`) runs the
//  editor's `ParameterChangeListener::update` synchronously on the calling
//  thread, and touching X11/cairo controls from any other thread segfaults.
//  So when the controller receives parameter changes off the UI thread while
//  an editor is open, it must not write the parameters immediately. Instead
//  it enqueues them here and applies them later on the UI thread through a
//  deferred `UpdateHandler` message (delivered by the editor's idle timer).
//
//  This queue is SDK-free so it can be unit-tested without a host. Only the
//  controller touches it, never the audio thread, so a plain mutex is fine.
// ============================================================================

#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace kraftonic::formats
{

struct PendingParamUpdate
{
    std::uint32_t id = 0;
    double normalized = 0.0;
};

/**
 * Ordered, coalescing queue of normalized parameter values.
 * Re-adding an already-queued id keeps its original position and adopts the
 * latest value, so a burst of background updates drains as minimal UI work.
 * All methods are thread-safe.
 */
class PendingParamUpdates
{
public:
    void add (std::uint32_t id, double normalized)
    {
        std::lock_guard<std::mutex> lock (mutex_);
        for (auto& queued : queued_)
        {
            if (queued.id == id)
            {
                queued.normalized = normalized;
                return;
            }
        }
        queued_.push_back (PendingParamUpdate {id, normalized});
    }

    std::vector<PendingParamUpdate> take()
    {
        std::lock_guard<std::mutex> lock (mutex_);
        std::vector<PendingParamUpdate> drained;
        drained.swap (queued_);
        return drained;
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock (mutex_);
        return queued_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::vector<PendingParamUpdate> queued_;
};

} // namespace kraftonic::formats
