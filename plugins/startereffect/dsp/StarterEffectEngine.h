// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic Starter Effect — the sound engine.
//
//  Signal flow (stereo, in place):
//
//      in ─> drive + tone ─> stereo delay (feedback, damping, ping-pong,
//            (fx::Drive)      dry/wet mix; fx::Delay)  ─> output level ─> out
//
//  The heavy lifting is done by two shared effects from modules/fx; this
//  class maps the product's parameters onto their plain-value Parameters
//  structs. That is the usual shape of a Kraftonic effect: shared building
//  blocks, product-specific wiring.
//
//  Real-time rules (AGENTS.md): prepare() may allocate (the delay lines);
//  reset(), setParameters() and process() never allocate, lock or throw.
// ============================================================================

#include "StarterEffectParameters.h"

#include <kraftonic/core/Instrument.h>
#include <kraftonic/core/Smoother.h>
#include <kraftonic/fx/Delay.h>
#include <kraftonic/fx/Drive.h>

namespace startereffect
{
namespace fx = kraftonic::fx;

using core::AudioBlock;
using core::ProcessSetup;

class StarterEffectEngine
{
public:
    void prepare (const ProcessSetup& setup);
    void reset() noexcept;
    void setParameters (const StarterEffectParameters& params) noexcept;
    /** Processes block.left / block.right in place. */
    void process (const AudioBlock& block) noexcept;
    /** No look-ahead or oversampling, so hosts need no delay compensation. */
    int latencySamples() const noexcept { return 0; }

private:
    fx::Drive drive_;
    fx::Delay delay_;
    core::LinearSmoother gain_; // output level, ramped over 20 ms
    bool snapGain_ = true;      // after reset(): jump to the next value, no ramp
};
} // namespace startereffect
