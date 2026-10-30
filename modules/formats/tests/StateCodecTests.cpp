// SPDX-License-Identifier: MIT
//
// StateCodec tests. Pins the generic versioned state format
// every product shares: header layout, per-parameter entries, unknown-ID
// tolerance, missing-ID defaults, and fail-closed corruption handling. The
// fake two-parameter traits below proves the codec is product-agnostic; the
// layout assertions double as the v1 compatibility contract products rely
// on (same magic/version/entry sizes, little-endian).

#include "kraftonic/formats/StateCodec.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

namespace
{

enum class FakeId : std::uint32_t
{
    gain = 0,
    steps = 1,
    count
};

struct FakeParams
{
    std::array<float, 2> values {};

    static FakeParams defaults() noexcept
    {
        FakeParams s {};
        s.values[0] = -6.0F;
        s.values[1] = 1.0F;
        return s;
    }

    float get (FakeId id) const noexcept
    {
        return values[static_cast<std::size_t> (id)];
    }

    void set (FakeId id, float plain) noexcept
    {
        values[static_cast<std::size_t> (id)] = plain;
    }
};

struct FakeTraits
{
    using ParamId = FakeId;
    using Params = FakeParams;
    static constexpr int kNumParams = 2;
    static constexpr std::uint32_t kStateMagic = 0x4754534Bu; // 'KSTG'
    static constexpr std::uint32_t kStateVersion = 1u;

    struct Info
    {
        FakeId id;
        float minValue;
        float maxValue;
    };

    static const Info* paramTable() noexcept
    {
        static constexpr Info table[2] {
            {FakeId::gain, -60.0F, 6.0F},
            {FakeId::steps, 0.0F, 2.0F},
        };
        return table;
    }

    static const Info& paramInfo (FakeId id) noexcept
    {
        return paramTable()[static_cast<std::size_t> (id)];
    }
};

} // namespace

using namespace kraftonic::formats;

TEST_CASE ("state round-trips all parameters", "[plugin-state]")
{
    FakeParams params = FakeParams::defaults();
    params.set (FakeId::gain, -3.25F);
    params.set (FakeId::steps, 2.0F);

    std::uint8_t bytes[kMaxStateBytes] {};
    const std::size_t used = encodeState<FakeTraits> (params, bytes, sizeof (bytes));
    CHECK (used == stateSizeForCount (2));

    FakeParams decoded = FakeParams::defaults();
    decoded.set (FakeId::gain, 0.0F);
    CHECK (decodeState<FakeTraits> (bytes, used, decoded));
    CHECK (decoded.get (FakeId::gain) == Catch::Approx (-3.25F));
    CHECK (decoded.get (FakeId::steps) == Catch::Approx (2.0F));
}

TEST_CASE ("state header matches the v1 layout", "[plugin-state]")
{
    FakeParams params = FakeParams::defaults();
    std::uint8_t bytes[kMaxStateBytes] {};
    CHECK (encodeState<FakeTraits> (params, bytes, sizeof (bytes)) > 0);
    CHECK (bytes[0] == 'K');
    CHECK (bytes[1] == 'S');
    CHECK (bytes[2] == 'T');
    CHECK (bytes[3] == 'G');
    CHECK (bytes[4] == 1); // version 1, little-endian
    CHECK (bytes[8] == 2); // two entries, little-endian
    CHECK (stateSizeForCount (2) == kStateHeaderSize + 2 * kStateEntrySize);
}

TEST_CASE ("state decode ignores unknown IDs", "[plugin-state]")
{
    FakeParams params = FakeParams::defaults();
    std::uint8_t bytes[kMaxStateBytes] {};
    const std::size_t used = encodeState<FakeTraits> (params, bytes, sizeof (bytes));
    REQUIRE (used > 0);
    bytes[kStateHeaderSize] = 0xFF; // first entry id -> unknown
    bytes[kStateHeaderSize + 1] = 0xFF;

    FakeParams decoded = FakeParams::defaults();
    decoded.set (FakeId::gain, 0.0F);
    CHECK (decodeState<FakeTraits> (bytes, used, decoded));
    CHECK (decoded.get (FakeId::gain) == Catch::Approx (-6.0F)); // default kept
    CHECK (decoded.get (FakeId::steps) == Catch::Approx (1.0F));
}

TEST_CASE ("state decode fails closed on corruption", "[plugin-state]")
{
    FakeParams out = FakeParams::defaults();
    out.set (FakeId::gain, 0.0F);

    const std::uint8_t empty[1] {};
    CHECK (!decodeState<FakeTraits> (nullptr, 0, out));
    CHECK (!decodeState<FakeTraits> (empty, 0, out));
    CHECK (!decodeState<FakeTraits> (empty, sizeof (empty), out));

    FakeParams params = FakeParams::defaults();
    std::uint8_t bytes[kMaxStateBytes] {};
    const std::size_t used = encodeState<FakeTraits> (params, bytes, sizeof (bytes));
    REQUIRE (used > 10);
    CHECK (!decodeState<FakeTraits> (bytes, used - 1, out)); // truncated entry
    bytes[0] = 'X'; // bad magic
    CHECK (!decodeState<FakeTraits> (bytes, used, out));
    CHECK (out.get (FakeId::gain) == Catch::Approx (-6.0F)); // defaults restored
}

TEST_CASE ("state decode clamps out-of-range values", "[plugin-state]")
{
    FakeParams params = FakeParams::defaults();
    std::uint8_t bytes[kMaxStateBytes] {};
    const std::size_t used = encodeState<FakeTraits> (params, bytes, sizeof (bytes));
    REQUIRE (used > 0);
    // Overwrite the gain entry's float64 with +100.0 (little-endian).
    std::uint64_t bits = 0x4059000000000000ull;
    for (int i = 0; i < 8; ++i)
        bytes[kStateHeaderSize + 4 + static_cast<std::size_t> (i)]
            = static_cast<std::uint8_t> ((bits >> (8 * i)) & 0xFFu);

    FakeParams decoded = FakeParams::defaults();
    CHECK (decodeState<FakeTraits> (bytes, used, decoded));
    CHECK (decoded.get (FakeId::gain) == Catch::Approx (6.0F)); // clamped to max
}

TEST_CASE ("state encode refuses a small buffer", "[plugin-state]")
{
    FakeParams params = FakeParams::defaults();
    std::uint8_t tiny[4] {};
    CHECK (encodeState<FakeTraits> (params, tiny, sizeof (tiny)) == 0);
    CHECK (encodeState<FakeTraits> (params, nullptr, kMaxStateBytes) == 0);
}
