// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic Starter Effect — factory presets.
//
//  A preset lists plain values by the stable parameter `key`; anything it
//  leaves out keeps its default from the parameter table. Hosts show the
//  presets in their program list and the panel's preset menu. Names must be
//  ASCII. Add a preset by writing a value array and a row in kPresets.
// ============================================================================

#include <kraftonic/formats/Preset.h>

#include <cstddef>
#include <iterator>

namespace startereffect::plugin
{
using kraftonic::formats::FactoryPreset;
using kraftonic::formats::PresetValue;

// Init is the parameter table's defaults: a light drive and a quiet echo.
inline constexpr PresetValue kInit[] {{"outputGain", 0.0F}};

// Slapback: one short, bright repeat and no drive.
inline constexpr PresetValue kSlapback[] {
    {"drive", 0.0F}, {"delayTime", 110.0F}, {"feedback", 0.1F}, {"damping", 9000.0F},
    {"pingPong", 0.0F}, {"mix", 0.3F}};

// Dark ping-pong: long repeats bouncing between the speakers, losing treble.
inline constexpr PresetValue kDarkPingPong[] {
    {"drive", 0.1F}, {"tone", 0.35F}, {"delayTime", 420.0F}, {"feedback", 0.55F},
    {"damping", 2500.0F}, {"pingPong", 1.0F}, {"mix", 0.35F}};

// Crunch echo: strong drive first, then a short rhythmic echo.
inline constexpr PresetValue kCrunchEcho[] {
    {"drive", 0.7F}, {"tone", 0.6F}, {"delayTime", 250.0F}, {"feedback", 0.3F},
    {"damping", 5000.0F}, {"mix", 0.25F}, {"outputGain", -4.0F}};

inline constexpr FactoryPreset kPresets[] {
    {"Init", kInit, std::size (kInit), "Basic"},
    {"Slapback", kSlapback, std::size (kSlapback), "Echo"},
    {"Dark Ping-Pong", kDarkPingPong, std::size (kDarkPingPong), "Echo"},
    {"Crunch Echo", kCrunchEcho, std::size (kCrunchEcho), "Drive"},
};
inline constexpr std::size_t kNumPresets = std::size (kPresets);

} // namespace startereffect::plugin
