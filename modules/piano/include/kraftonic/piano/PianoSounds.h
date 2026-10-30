// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic piano module: the piano sounds a PianoLayer can play.
//
//  A sound is chosen by type and a model within the type. The flat index
//  (kModels order) is permanent once a product ships it, because products store
//  it in a parameter. Append new models at the end of kModels; never
//  reorder or reuse an entry.
//
//  `source` says how a model is made:
//    modelled - synthesized physical/FM model (modules/piano/src)
//    sampled  - recordings played by modules/sampler: the grands from the
//               "grand" instrument folder, the other sampled models from
//               their own folder (PianoLayer::modelFolder); when the data is
//               missing the layer plays a clearly quieter modelled stand-in
//               and reports it
// ============================================================================

#include <array>
#include <cstddef>
#include <cstdint>

namespace kraftonic::piano
{
enum class Type : std::uint8_t
{
    grand,
    electric,
    clav
};

inline constexpr int kNumTypes = 3;

enum class Source : std::uint8_t
{
    modelled,
    sampled
};

struct ModelInfo
{
    Type type;
    const char* name; // display name (original; never a trademark)
    Source source;
};

// clang-format off
inline constexpr std::array<ModelInfo, 14> kModels {{
    { Type::electric, "Tine EP",        Source::modelled }, // 0
    { Type::electric, "Reed EP",        Source::modelled }, // 1
    { Type::electric, "Electric Grand", Source::modelled }, // 2
    { Type::clav,     "Clav",           Source::modelled }, // 3
    { Type::grand,    "Bright Grand",   Source::sampled  }, // 4: bright, punchy
    { Type::grand,    "Studio Grand",   Source::sampled  }, // 5: same recordings, warmer and wider
    { Type::electric, "Bright Tine",    Source::modelled }, // 6
    { Type::electric, "Hard Reed",      Source::modelled }, // 7
    { Type::electric, "Glass Reed",     Source::modelled }, // 8
    { Type::clav,     "Bright Clav",    Source::modelled }, // 9
    { Type::electric, "Celesta",        Source::modelled }, // 10
    { Type::clav,     "Harpsichord",    Source::sampled  }, // 11: plucked strings, recorded releases
    { Type::electric, "Vibes",          Source::sampled  }, // 12: metal bars over resonators
    { Type::electric, "Marimba",        Source::sampled  }, // 13: wooden bars over resonators
}};
// clang-format on

inline constexpr int kNumModels = static_cast<int> (kModels.size());

/** Number of models of `type`. */
constexpr int modelCount (Type type) noexcept
{
    int count = 0;
    for (const auto& info : kModels)
        count += info.type == type ? 1 : 0;
    return count;
}

/** Flat kModels index of the `model`-th model of `type` (clamped to the
    type's models). */
constexpr int modelIndex (Type type, int model) noexcept
{
    const int last = modelCount (type) - 1;
    const int wanted = model < 0 ? 0 : (model > last ? last : model);
    int seen = 0;
    for (int i = 0; i < kNumModels; ++i)
        if (kModels[static_cast<std::size_t> (i)].type == type && seen++ == wanted)
            return i;
    return 0;
}

/** Position of flat index `index` within its type (inverse of modelIndex). */
constexpr int modelWithinType (int index) noexcept
{
    if (index < 0 || index >= kNumModels)
        return 0;
    int position = 0;
    for (int i = 0; i < index; ++i)
        position += kModels[static_cast<std::size_t> (i)].type == kModels[static_cast<std::size_t> (index)].type ? 1 : 0;
    return position;
}
} // namespace kraftonic::piano
