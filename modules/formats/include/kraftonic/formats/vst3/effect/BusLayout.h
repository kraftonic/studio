// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Effect bus layout rules.
//
//  SDK-free: speaker arrangements are the VST3 bit masks, mirrored here so
//  the acceptance logic unit-tests without the SDK. Vst3EffectProcessor.h
//  static_asserts that these constants match Steinberg::Vst::SpeakerArr.
//
//  A Kraftonic effect has exactly one main audio input and one main audio
//  output. Accepted:
//      stereo in -> stereo out   (default)
//      mono   in -> stereo out   (a mono source into a stereo effect)
//  Everything else is refused so the host falls back to one of these.
// ============================================================================

#include <cstdint>

namespace kraftonic::formats::vst3::effect
{
using SpeakerArrangement = std::uint64_t;

inline constexpr SpeakerArrangement kSpeakerMono = 1ull << 19;            // kSpeakerM
inline constexpr SpeakerArrangement kSpeakerStereo = (1ull << 0) | (1ull << 1); // kSpeakerL | kSpeakerR

enum class InputLayout
{
    stereo,
    mono,
};

/** True when (inputs, outputs) is one of the accepted effect layouts. */
constexpr bool acceptsBusArrangement (const SpeakerArrangement* inputs, int numIns,
                                      const SpeakerArrangement* outputs, int numOuts) noexcept
{
    if (numIns != 1 || numOuts != 1 || inputs == nullptr || outputs == nullptr)
        return false;
    if (outputs[0] != kSpeakerStereo)
        return false;
    return inputs[0] == kSpeakerStereo || inputs[0] == kSpeakerMono;
}

constexpr InputLayout inputLayoutFor (SpeakerArrangement input) noexcept
{
    return input == kSpeakerMono ? InputLayout::mono : InputLayout::stereo;
}

/** Number of channels in an arrangement (popcount), for buffer checks. */
constexpr int channelCount (SpeakerArrangement arrangement) noexcept
{
    int count = 0;
    for (; arrangement != 0; arrangement &= arrangement - 1)
        ++count;
    return count;
}

} // namespace kraftonic::formats::vst3::effect
