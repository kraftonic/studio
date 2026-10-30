// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic plugin — generic factory-preset application.
//
//  Presets are plain values keyed by the contract's stable string `key`,
//  compiled into the binary (no file I/O). This helper turns one preset into
//  a product parameter struct through any traits type that exposes:
//      using ParamId; using Params;        // Params::defaults/get/set
//      kNumParams (int), paramTable()       // table entries with .id + .key
//  Unknown keys are ignored; missing keys keep their defaults; values are
//  clamped by Params::set. SDK-free and noexcept so both the processor
//  (program-change handling) and the controller (program-list selection)
//  share one implementation.
// ============================================================================

#include <cstddef>
#include <cstring>

namespace kraftonic::formats
{

/** Minimal preset shape every product traits type provides. */
struct PresetValue
{
    const char* key; // stable contract key from ParamInfo::key
    float plain;     // plain (engine-unit) value
};

struct FactoryPreset
{
    const char* name;
    const PresetValue* values;
    std::size_t count;
    // Optional category (e.g. "Bass", "Lead"); nullptr means uncategorised.
    // Defaulted so existing three-field presets keep compiling unchanged.
    const char* category = nullptr;
};

namespace detail
{

inline bool keysEqual (const char* a, const char* b) noexcept
{
    if (a == nullptr || b == nullptr)
        return a == b;
    return std::strcmp (a, b) == 0;
}

} // namespace detail

/**
 * Builds a parameter struct from a preset. Starts from Params::defaults(),
 * then applies each preset entry whose key matches the traits table.
 */
template <typename Traits>
typename Traits::Params presetToParameters (const FactoryPreset& preset) noexcept
{
    typename Traits::Params params = Traits::Params::defaults();
    if (preset.values == nullptr)
        return params;
    const auto* table = Traits::paramTable();
    for (std::size_t i = 0; i < preset.count; ++i)
    {
        const char* key = preset.values[i].key;
        if (key == nullptr)
            continue;
        for (int p = 0; p < Traits::kNumParams; ++p)
        {
            if (detail::keysEqual (table[p].key, key))
            {
                params.set (table[p].id, preset.values[i].plain);
                break;
            }
        }
    }
    return params;
}

/** Finds a preset by index with bounds checking (nullptr when out of range). */
template <typename Traits>
const FactoryPreset* presetAt (int index) noexcept
{
    if (index < 0 || index >= static_cast<int> (Traits::numPresets()))
        return nullptr;
    return &Traits::presets()[static_cast<std::size_t> (index)];
}

} // namespace kraftonic::formats
