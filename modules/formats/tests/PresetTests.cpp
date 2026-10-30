// SPDX-License-Identifier: MIT
//
// Preset tests. Pins key-based preset application: known
// keys land on the right parameters, unknown keys are ignored, missing keys
// keep defaults, and values are clamped by the parameter struct.

#include "kraftonic/formats/Preset.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

enum class FakeId : std::uint32_t
{
    gain = 0,
    wave = 1,
    count
};

struct FakeParams
{
    std::array<float, 2> values {};

    static FakeParams defaults() noexcept
    {
        FakeParams s {};
        s.values[0] = -6.0F;
        s.values[1] = 0.0F;
        return s;
    }

    float get (FakeId id) const noexcept
    {
        return values[static_cast<std::size_t> (id)];
    }

    void set (FakeId id, float plain) noexcept
    {
        float v = plain;
        if (id == FakeId::gain)
        {
            if (v < -60.0F)
                v = -60.0F;
            if (v > 6.0F)
                v = 6.0F;
        }
        else
        {
            if (v < 0.0F)
                v = 0.0F;
            if (v > 2.0F)
                v = 2.0F;
        }
        values[static_cast<std::size_t> (id)] = v;
    }
};

struct FakeTableEntry
{
    FakeId id;
    const char* key;
};

struct FakeTraits
{
    using ParamId = FakeId;
    using Params = FakeParams;
    static constexpr int kNumParams = 2;

    static const FakeTableEntry* paramTable() noexcept
    {
        static constexpr FakeTableEntry table[2] {
            {FakeId::gain, "gain"},
            {FakeId::wave, "wave"},
        };
        return table;
    }

    static std::size_t numPresets() noexcept
    {
        return 2;
    }

    static const kraftonic::formats::FactoryPreset* presets() noexcept
    {
        using kraftonic::formats::PresetValue;
        using kraftonic::formats::FactoryPreset;
        static constexpr PresetValue first[] = {{"gain", -9.0F}, {"wave", 1.0F}};
        static constexpr PresetValue second[] = {{"gain", -3.0F}};
        static constexpr FactoryPreset table[2] {
            {"First", first, 2},
            {"Partial", second, 1},
        };
        return table;
    }
};

} // namespace

using namespace kraftonic::formats;

TEST_CASE ("presets apply by stable key", "[plugin-preset]")
{
    const FactoryPreset* first = presetAt<FakeTraits> (0);
    REQUIRE (first != nullptr);
    const FakeParams params = presetToParameters<FakeTraits> (*first);
    CHECK (params.get (FakeId::gain) == Catch::Approx (-9.0F));
    CHECK (params.get (FakeId::wave) == Catch::Approx (1.0F));
}

TEST_CASE ("presets leave missing keys at defaults", "[plugin-preset]")
{
    const FactoryPreset* partial = presetAt<FakeTraits> (1);
    REQUIRE (partial != nullptr);
    const FakeParams params = presetToParameters<FakeTraits> (*partial);
    CHECK (params.get (FakeId::gain) == Catch::Approx (-3.0F));
    CHECK (params.get (FakeId::wave) == Catch::Approx (0.0F)); // default
}

TEST_CASE ("presets ignore unknown keys and clamp values", "[plugin-preset]")
{
    const PresetValue values[] = {{"nope", 5.0F}, {"gain", 100.0F}, {nullptr, 1.0F}};
    const FactoryPreset odd {"Odd", values, 3};
    const FakeParams params = presetToParameters<FakeTraits> (odd);
    CHECK (params.get (FakeId::gain) == Catch::Approx (6.0F)); // clamped
    CHECK (params.get (FakeId::wave) == Catch::Approx (0.0F));
}

TEST_CASE ("preset lookup is bounds-checked", "[plugin-preset]")
{
    CHECK (presetAt<FakeTraits> (-1) == nullptr);
    CHECK (presetAt<FakeTraits> (2) == nullptr);
    CHECK (presetAt<FakeTraits> (99) == nullptr);
}
