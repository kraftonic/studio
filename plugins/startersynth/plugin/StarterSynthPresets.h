// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic Starter Synth — factory presets.
//
//  A preset lists plain values by the stable parameter `key`; anything it
//  leaves out keeps its default from the parameter table. Hosts show the
//  presets in their program list and the panel's preset menu. Names must be
//  ASCII. Add a preset by writing a value array and a row in kPresets.
// ============================================================================

#include <kraftonic/formats/Preset.h>

#include <cstddef>
#include <iterator>

namespace startersynth::plugin
{
using kraftonic::formats::FactoryPreset;
using kraftonic::formats::PresetValue;

// Init is the parameter table's defaults: a bright two-saw sound.
inline constexpr PresetValue kInit[] {{"outputGain", -6.0F}};

// Warm pad: slow attack and release, soft square layer, gentle filter.
inline constexpr PresetValue kWarmPad[] {
    {"osc1Wave", 0.0F}, {"osc2Wave", 1.0F}, {"osc2Detune", 12.0F}, {"oscMix", 0.45F},
    {"cutoff", 1200.0F}, {"resonance", 0.15F}, {"envAmount", 1.0F},
    {"attack", 0.6F}, {"decay", 1.5F}, {"sustain", 0.8F}, {"release", 1.8F}, {"outputGain", -8.0F}};

// Pluck: no sustain, the envelope snaps the filter shut.
inline constexpr PresetValue kPluck[] {
    {"osc1Wave", 0.0F}, {"osc2Wave", 0.0F}, {"osc2Detune", -6.0F}, {"cutoff", 300.0F},
    {"resonance", 0.35F}, {"envAmount", 3.5F}, {"attack", 0.001F}, {"decay", 0.25F},
    {"sustain", 0.0F}, {"release", 0.25F}, {"outputGain", -6.0F}};

// Glide bass: square plus sine, each new note slides from the last one.
inline constexpr PresetValue kGlideBass[] {
    {"osc1Wave", 1.0F}, {"osc2Wave", 2.0F}, {"osc2Detune", 0.0F}, {"oscMix", 0.4F},
    {"cutoff", 400.0F}, {"resonance", 0.4F}, {"envAmount", 2.0F}, {"attack", 0.002F},
    {"decay", 0.4F}, {"sustain", 0.5F}, {"release", 0.1F}, {"glide", 0.08F}, {"outputGain", -6.0F}};

// Soft keys: sine plus a quiet saw, a medium decay like a struck tone.
inline constexpr PresetValue kSoftKeys[] {
    {"osc1Wave", 2.0F}, {"osc2Wave", 0.0F}, {"osc2Detune", 3.0F}, {"oscMix", 0.2F},
    {"cutoff", 2500.0F}, {"resonance", 0.1F}, {"envAmount", 1.5F}, {"attack", 0.003F},
    {"decay", 1.2F}, {"sustain", 0.3F}, {"release", 0.5F}, {"outputGain", -6.0F}};

inline constexpr FactoryPreset kPresets[] {
    {"Init", kInit, std::size (kInit), "Basic"},
    {"Warm Pad", kWarmPad, std::size (kWarmPad), "Pad"},
    {"Pluck", kPluck, std::size (kPluck), "Pluck"},
    {"Glide Bass", kGlideBass, std::size (kGlideBass), "Bass"},
    {"Soft Keys", kSoftKeys, std::size (kSoftKeys), "Keys"},
};
inline constexpr std::size_t kNumPresets = std::size (kPresets);

} // namespace startersynth::plugin
