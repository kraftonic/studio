// SPDX-License-Identifier: MIT
//
// Shared effect helper tests. SDK-free, so they
// run in the fast DSP loop: bus-layout acceptance, the sample-accurate
// parameter schedule and the latency-compensated bypass.

#include "kraftonic/formats/LatencyBypass.h"
#include "kraftonic/formats/ParamSchedule.h"
#include "kraftonic/formats/vst3/effect/BusLayout.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

namespace
{
using kraftonic::formats::vst3::effect::acceptsBusArrangement;
using kraftonic::formats::vst3::effect::channelCount;
using kraftonic::formats::vst3::effect::InputLayout;
using kraftonic::formats::vst3::effect::inputLayoutFor;
using kraftonic::formats::vst3::effect::kSpeakerMono;
using kraftonic::formats::vst3::effect::kSpeakerStereo;
using kraftonic::formats::LatencyBypass;
using kraftonic::formats::ParamSchedule;
} // namespace

TEST_CASE ("effect accepts stereo and mono inputs to a stereo output", "[effect]")
{
    const auto inStereo = kSpeakerStereo;
    const auto inMono = kSpeakerMono;
    const auto out = kSpeakerStereo;
    CHECK (acceptsBusArrangement (&inStereo, 1, &out, 1));
    CHECK (acceptsBusArrangement (&inMono, 1, &out, 1));
    CHECK_FALSE (acceptsBusArrangement (&inMono, 1, &inMono, 1)); // mono out refused
    CHECK_FALSE (acceptsBusArrangement (nullptr, 1, &out, 1));
    CHECK_FALSE (acceptsBusArrangement (&inStereo, 0, &out, 1));
    CHECK_FALSE (acceptsBusArrangement (&inStereo, 1, &out, 0));
    CHECK (inputLayoutFor (kSpeakerMono) == InputLayout::mono);
    CHECK (inputLayoutFor (kSpeakerStereo) == InputLayout::stereo);
    CHECK (channelCount (kSpeakerMono) == 1);
    CHECK (channelCount (kSpeakerStereo) == 2);
}

TEST_CASE ("schedule applies each point from its own offset", "[effect]")
{
    ParamSchedule<64, 4> schedule {};
    schedule.beginLane();
    schedule.push (10, 7, 1.0F, 64);
    schedule.push (40, 7, 2.0F, 64);
    CHECK (schedule.nextOffset() == 10);
    // Sub-blocks never get shorter than kMinSubBlock, so a point at offset
    // 10 lands within 16 samples (the documented walker granularity).
    CHECK (schedule.subBlockEnd (0, 64) == ParamSchedule<64, 4>::kMinSubBlock);
    std::vector<std::pair<std::uint32_t, float>> applied {};
    CHECK_FALSE (schedule.applyUpTo (9, [&] (std::uint32_t id, float v) noexcept {
        applied.emplace_back (id, v);
    }));
    CHECK (applied.empty());
    CHECK (schedule.applyUpTo (10, [&] (std::uint32_t id, float v) noexcept {
        applied.emplace_back (id, v);
    }));
    REQUIRE (applied.size() == 1);
    CHECK (applied[0].first == 7);
    CHECK (applied[0].second == Catch::Approx (1.0F));
    CHECK (schedule.nextOffset() == 40);
    CHECK (schedule.subBlockEnd (20, 64) == 40);
}

TEST_CASE ("schedule clamps offsets and keeps the last value when full", "[effect]")
{
    ParamSchedule<2, 1> tiny {};
    REQUIRE (tiny.beginLane());
    tiny.push (-5, 1, 1.0F, 64);   // clamps to 0
    tiny.push (999, 1, 2.0F, 64);  // clamps to numSamples - 1
    tiny.push (30, 1, 3.0F, 64);   // store full: overwrites the lane's last point
    CHECK (tiny.numPoints() == 2);
    float last = 0.0F;
    tiny.applyUpTo (ParamSchedule<2, 1>::kNoMorePoints,
                    [&] (std::uint32_t /*id*/, float v) noexcept { last = v; });
    CHECK (last == Catch::Approx (3.0F)); // the final value is never lost
}

TEST_CASE ("bypassed output lines up with the engine latency", "[effect]")
{
    constexpr int latency = 32;
    LatencyBypass bypass {};
    bypass.prepare (48000.0, latency);
    bypass.reset();
    // Wet passthrough: mix at 0 leaves the engine output untouched.
    std::vector<float> dry (128, 1.0F);
    std::vector<float> wet (128, 5.0F);
    std::vector<float> wetRight (128, 5.0F);
    std::vector<float> dryRight (128, 1.0F);
    bypass.setBypassed (false);
    bypass.snapToTarget();
    bypass.process (dry.data(), dryRight.data(), wet.data(), wetRight.data(), 128);
    CHECK (wet[100] == Catch::Approx (5.0F));
    // Fully bypassed: output is the dry signal delayed by exactly latency.
    bypass.setBypassed (true);
    bypass.snapToTarget();
    std::vector<float> out (128, -1.0F);
    std::vector<float> outRight (128, -1.0F);
    std::vector<float> ramp (128, 0.0F);
    for (int i = 0; i < 128; ++i)
        ramp[i] = static_cast<float> (i);
    bypass.process (ramp.data(), ramp.data(), out.data(), outRight.data(), 128);
    for (int i = latency; i < 128; ++i)
        CHECK (out[i] == Catch::Approx (ramp[static_cast<std::size_t> (i - latency)]));
    CHECK (bypass.fullyBypassed());
    CHECK_FALSE (bypass.fullyActive());
}
