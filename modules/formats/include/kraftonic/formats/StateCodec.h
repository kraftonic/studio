// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic plugin — generic versioned state codec.
//
//  One binary format for every product's project state, used by the VST3
//  processor's getState/setState and the controller's setComponentState:
//
//      uint32 magic   = Traits::kStateMagic (per product, four ASCII characters)
//      uint32 version = Traits::kStateVersion (currently 1 everywhere)
//      uint32 count
//      count x (uint32 paramId, float64 plainValue)   // little-endian
//
//  The layout is stable: a product that adopts this codec with its own
//  magic keeps reading its earlier sessions. Rules: unknown IDs are ignored, missing IDs fall back to
//  defaults, and corrupt or short data leaves the defaults in place and
//  reports failure — it never crashes, reads out of bounds, or throws.
//
//  Any traits type with kStateMagic/kStateVersion/kNumParams, paramTable(),
//  paramInfo(id) (entries with .minValue/.maxValue) and a Params type with
//  defaults()/get()/set() works here. SDK-free so it unit-tests without a
//  host; adapters move the same bytes through IBStreamer.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace kraftonic::formats
{

inline constexpr std::size_t kStateHeaderSize = 12;
inline constexpr std::size_t kStateEntrySize = 12; // uint32 id + float64 value
inline constexpr std::size_t kMaxStateBytes = 4096;

namespace state_detail
{

inline void writeU32LE (std::uint8_t* dst, std::uint32_t v) noexcept
{
    dst[0] = static_cast<std::uint8_t> (v & 0xFFu);
    dst[1] = static_cast<std::uint8_t> ((v >> 8) & 0xFFu);
    dst[2] = static_cast<std::uint8_t> ((v >> 16) & 0xFFu);
    dst[3] = static_cast<std::uint8_t> ((v >> 24) & 0xFFu);
}

inline std::uint32_t readU32LE (const std::uint8_t* src) noexcept
{
    return static_cast<std::uint32_t> (src[0])
        | (static_cast<std::uint32_t> (src[1]) << 8)
        | (static_cast<std::uint32_t> (src[2]) << 16)
        | (static_cast<std::uint32_t> (src[3]) << 24);
}

inline void writeF64LE (std::uint8_t* dst, double v) noexcept
{
    static_assert (sizeof (double) == 8, "float64 state needs 8-byte doubles");
    std::uint64_t bits = 0;
    std::memcpy (&bits, &v, sizeof (bits));
    for (int i = 0; i < 8; ++i)
        dst[i] = static_cast<std::uint8_t> ((bits >> (8 * i)) & 0xFFu);
}

inline double readF64LE (const std::uint8_t* src) noexcept
{
    static_assert (sizeof (double) == 8, "float64 state needs 8-byte doubles");
    std::uint64_t bits = 0;
    for (int i = 0; i < 8; ++i)
        bits |= static_cast<std::uint64_t> (src[i]) << (8 * i);
    double v = 0.0;
    std::memcpy (&v, &bits, sizeof (v));
    return v;
}

} // namespace state_detail

/** Bytes needed to store `count` entries (header + entries). */
constexpr std::size_t stateSizeForCount (std::size_t count) noexcept
{
    return kStateHeaderSize + count * kStateEntrySize;
}

/**
 * Serialises plain parameters. Writes header + one entry per contract
 * parameter. Returns bytes written, or 0 when `capacity` is too small.
 */
template <typename Traits>
std::size_t encodeState (
    const typename Traits::Params& params, std::uint8_t* dst, std::size_t capacity) noexcept
{
    const std::size_t need
        = stateSizeForCount (static_cast<std::size_t> (Traits::kNumParams));
    if (dst == nullptr || capacity < need)
        return 0;
    state_detail::writeU32LE (dst + 0, Traits::kStateMagic);
    state_detail::writeU32LE (dst + 4, Traits::kStateVersion);
    state_detail::writeU32LE (dst + 8, static_cast<std::uint32_t> (Traits::kNumParams));
    std::size_t at = kStateHeaderSize;
    const auto* table = Traits::paramTable();
    for (int p = 0; p < Traits::kNumParams; ++p)
    {
        state_detail::writeU32LE (dst + at, static_cast<std::uint32_t> (table[p].id));
        state_detail::writeF64LE (dst + at + 4,
                                 static_cast<double> (params.get (table[p].id)));
        at += kStateEntrySize;
    }
    return need;
}

/**
 * Parses state bytes into `out`. Starts from defaults, applies known IDs,
 * ignores unknown IDs. Returns true on a fully valid buffer; on ANY
 * corruption (short data, bad magic, version mismatch, truncated entries)
 * `out` holds the defaults and the result is false.
 */
template <typename Traits>
bool decodeState (const std::uint8_t* src, std::size_t size,
                  typename Traits::Params& out) noexcept
{
    out = Traits::Params::defaults();
    if (src == nullptr || size < kStateHeaderSize)
        return false;
    if (state_detail::readU32LE (src + 0) != Traits::kStateMagic)
        return false;
    if (state_detail::readU32LE (src + 4) != Traits::kStateVersion)
        return false;
    const std::uint32_t count = state_detail::readU32LE (src + 8);
    if (count > 1024) // sanity cap: a corrupt count must not pass validation
        return false;
    if (size < stateSizeForCount (count))
        return false;
    std::size_t at = kStateHeaderSize;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const std::uint32_t id = state_detail::readU32LE (src + at);
        const double plain = state_detail::readF64LE (src + at + 4);
        at += kStateEntrySize;
        if (id >= static_cast<std::uint32_t> (Traits::kNumParams))
            continue; // unknown ID: ignore
        const auto paramId = static_cast<typename Traits::ParamId> (id);
        const auto& info = Traits::paramInfo (paramId);
        float v = static_cast<float> (plain);
        if (!(v >= info.minValue && v <= info.maxValue))
        {
            // Clamp out-of-range values instead of rejecting the whole chunk.
            if (!(v > -1e30F && v < 1e30F)) // NaN or huge: keep the default
                continue;
            v = v < info.minValue ? info.minValue : info.maxValue;
        }
        out.set (paramId, v);
    }
    return true;
}

} // namespace kraftonic::formats
