// SPDX-License-Identifier: MIT
// Plug-in layer tests without a host: the traits satisfy the base contract,
// state round-trips, presets apply, value text round-trips, names are original.
#include <kraftonic/formats/StateCodec.h>
#include <kraftonic/formats/vst3/Vst3Traits.h>
#include <kraftonic/formats/vst3/effect/EffectTraits.h>
#include "StarterEffectTraits.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cctype>
#include <cstring>
#include <string>

using namespace startereffect;
using namespace startereffect::plugin;
using Traits = StarterEffectTraits;

// A traits mistake fails here with a readable message, not deep in the SDK.
static_assert (kraftonic::formats::vst3::effect::EffectTraits<Traits>);

TEST_CASE ("parameter ids are dense and keys unique")
{
    for (int i = 0; i < kNumParams; ++i)
    {
        CHECK (static_cast<int> (kParamInfo[static_cast<std::size_t> (i)].id) == i);
        for (int j = i + 1; j < kNumParams; ++j)
            CHECK (std::strcmp (kParamInfo[static_cast<std::size_t> (i)].key, kParamInfo[static_cast<std::size_t> (j)].key) != 0);
    }
}

TEST_CASE ("state round trip covers every parameter and rejects corruption")
{
    auto input = StarterEffectParameters::defaults();
    for (const auto& p : kParamInfo)
        input.set (p.id, toPlain (p.id, 0.37));
    std::array<std::uint8_t, kraftonic::formats::kMaxStateBytes> bytes {};
    const auto used = kraftonic::formats::encodeState<Traits> (input, bytes.data(), bytes.size());
    REQUIRE (used == kraftonic::formats::stateSizeForCount (kNumParams));

    StarterEffectParameters output;
    REQUIRE (kraftonic::formats::decodeState<Traits> (bytes.data(), used, output));
    CHECK (input.values == output.values);
    CHECK_FALSE (kraftonic::formats::decodeState<Traits> (bytes.data(), used - 1, output));
    bytes[0] ^= 0xFF; // another product's magic
    CHECK_FALSE (kraftonic::formats::decodeState<Traits> (bytes.data(), used, output));
}

TEST_CASE ("every preset key exists and every preset changes the sound")
{
    REQUIRE (Traits::numPresets() >= 3);
    for (std::size_t i = 0; i < Traits::numPresets(); ++i)
    {
        const auto& preset = Traits::presets()[i];
        for (std::size_t v = 0; v < preset.count; ++v)
        {
            bool known = false;
            for (const auto& p : kParamInfo)
                known = known || std::strcmp (p.key, preset.values[v].key) == 0;
            CHECK (known);
        }
        if (i > 0) // presets after Init must differ from it
            CHECK (kraftonic::formats::presetToParameters<Traits> (preset).values
                   != kraftonic::formats::presetToParameters<Traits> (Traits::presets()[0]).values);
    }
}

TEST_CASE ("value text round trips for every parameter")
{
    for (const auto& p : kParamInfo)
        for (double n : {0.0, 0.25, 0.5, 0.8, 1.0})
        {
            const float plain = toPlain (p.id, n);
            char text[64] {};
            formatPlainValue (p.id, plain, text, sizeof (text));
            float parsed = -1.0F;
            INFO (p.key << " = " << text);
            REQUIRE (parsePlainValue (p.id, text, parsed));
            // Display text is rounded, so compare in normalized units.
            CHECK (toNormalized (p.id, parsed) == Catch::Approx (toNormalized (p.id, plain)).margin (0.01));
        }
    float plain = 0.0F;
    CHECK_FALSE (parsePlainValue (ParamId::damping, "dark", plain));
    CHECK (parsePlainValue (ParamId::damping, "2k", plain));
    CHECK (plain == Catch::Approx (2000.0F));
}

TEST_CASE ("the tail covers the echoes and stays finite")
{
    auto p = StarterEffectParameters::defaults();
    p.set (ParamId::feedback, 0.0F);
    CHECK (Traits::tailSeconds (p) >= p.get (ParamId::delayTime) / 1000.0);
    p.set (ParamId::feedback, 0.5F); // 10 repeats to fall 60 dB
    CHECK (Traits::tailSeconds (p) >= 10.0 * p.get (ParamId::delayTime) / 1000.0);
    for (const auto& info : kParamInfo)
        p.set (info.id, info.maxValue);
    CHECK (Traits::tailSeconds (p) <= 60.0);
}

TEST_CASE ("public strings use original names only")
{
    // Encoded so the list itself does not put maker names in the source.
    const char* banned[] {"\x72\x68\x6f\x64\x65\x73", "\x77\x75\x72\x6c\x69\x74\x7a\x65\x72", "\x68\x61\x6d\x6d\x6f\x6e\x64",
                          "\x6c\x65\x73\x6c\x69\x65", "\x6e\x6f\x72\x64", "\x6d\x6f\x6f\x67", "\x72\x6f\x6c\x61\x6e\x64",
                          "\x73\x65\x71\x75\x65\x6e\x74\x69\x61\x6c", "\x6f\x62\x65\x72\x68\x65\x69\x6d",
                          "\x79\x61\x6d\x61\x68\x61", "\x6b\x6f\x72\x67"};
    auto check = [&] (const char* text)
    {
        REQUIRE (text != nullptr);
        std::string lower (text);
        for (char& c : lower)
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        for (const char* word : banned)
            CHECK (lower.find (word) == std::string::npos);
    };
    check (Traits::kPluginName);
    for (const auto& p : kParamInfo)
    {
        check (p.key);
        check (p.name);
    }
    for (std::size_t i = 0; i < Traits::numPresets(); ++i)
        check (Traits::presets()[i].name);
}
