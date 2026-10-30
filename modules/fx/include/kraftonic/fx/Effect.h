// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic fx — EFFECT CONTRACT.
//
//  Every effect in modules/fx is a plain C++ class with this shape:
//
//      struct Parameters { ... plain values ... };   // trivially copyable
//      void prepare (const core::ProcessSetup&);      // NOT real-time; may allocate
//      void reset() noexcept;                         // real-time: clear tails/state
//      void setParameters (const Parameters&) noexcept;   // real-time, once per block
//      void process (float* left, float* right, int numSamples) noexcept; // in place
//      int latencySamples() const noexcept;           // 0 for almost everything
//
//  Optional: void setTempo (double bpm) noexcept; for tempo-synced effects.
//
//  Rules:
//   - Effects know nothing about any product's ParamId. Products own their
//     parameter tables and fill the effect's Parameters struct each block.
//   - Parameters are plain values (Hz, ms, dB, 0..1). Each Parameters struct
//     has `bool enabled` and, where it makes sense, `float mix` (0..1).
//   - Turning an effect on/off, and every continuous change, must be
//     click-free (smooth or crossfade internally).
//   - Real-time methods never allocate, lock, do I/O or throw. Delay lines
//     and buffers are sized in prepare() for the largest supported setting.
//   - Output is always finite. A disabled effect is bit-exact pass-through
//     once its crossfade has finished.
//   - Stereo in, stereo out. `right` may equal `left` for mono use.
//   - Results must not depend on how the host splits blocks.
// ============================================================================

#include "kraftonic/core/Instrument.h"

#include <concepts>

namespace kraftonic::fx
{
using core::ProcessSetup;

template <typename E>
concept StereoEffect = requires (E effect, const E constEffect, const typename E::Parameters& params,
                                 const ProcessSetup& setup, float* buffer, int numSamples) {
    typename E::Parameters;
    { effect.prepare (setup) };
    { effect.reset() } noexcept;
    { effect.setParameters (params) } noexcept;
    { effect.process (buffer, buffer, numSamples) } noexcept;
    { constEffect.latencySamples() } noexcept -> std::convertible_to<int>;
};
} // namespace kraftonic::fx
