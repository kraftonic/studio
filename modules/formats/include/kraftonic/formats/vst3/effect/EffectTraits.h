// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Effect product traits.
//
//  SDK-free. An effect reuses the whole PluginTraits contract (identity,
//  parameter table, presets, formatting, state magic) so the shared
//  Controller<Traits> works unchanged, and adds the effect engine shape:
//      engine.process (block)        in place, stereo
//      engine.latencySamples()       fixed latency, changes only in prepare()
//
//  PluginTraits still demands the instrument-shaped
//  process (block, events, count); EffectEngineAdapter supplies it so an
//  effect engine never has to know about events. The split (identity +
//  engine-shape concepts, no adapter) stays future work: keeping the adapter
//  keeps the promotion behaviour-identical.
// ============================================================================

#include "kraftonic/core/Instrument.h"
#include "kraftonic/formats/vst3/Vst3Traits.h"

#include <concepts>

namespace kraftonic::formats::vst3::effect
{

template <typename T>
concept EffectTraits = kraftonic::formats::vst3::PluginTraits<T>
    && requires (typename T::Engine& engine, const typename T::Engine& cengine,
                 const core::AudioBlock& block) {
           engine.process (block);
           { cengine.latencySamples() } -> std::convertible_to<int>;
       };

/** Wraps an effect engine so it also satisfies the instrument-shaped
    PluginTraits check. Events are ignored: effects take none. */
template <typename EffectEngine>
class EffectEngineAdapter : public EffectEngine
{
public:
    using EffectEngine::EffectEngine;
    using EffectEngine::process;

    void process (const core::AudioBlock& block, const core::Event* /*events*/,
                  int /*numEvents*/) noexcept
    {
        EffectEngine::process (block);
    }
};

} // namespace kraftonic::formats::vst3::effect
