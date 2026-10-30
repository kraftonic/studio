// SPDX-License-Identifier: MIT
#include "kraftonic/core/Parameters.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace
{
enum class TestId : std::uint32_t
{
    gain,
    time,
    mode
};
using Info = kraftonic::core::ParamInfo<TestId>;
using kraftonic::core::ParamScale;

constexpr Info kGain { TestId::gain, "gain", "Gain", "dB", -60.0f, 6.0f, -6.0f, ParamScale::linear };
constexpr Info kTime { TestId::time, "time", "Time", "s", 0.001f, 10.0f, 0.1f, ParamScale::logarithmic };
constexpr Info kMode { TestId::mode, "mode", "Mode", "", 0.0f, 2.0f, 0.0f, ParamScale::linear, 2 };
} // namespace

TEST_CASE ("Linear and logarithmic parameters round-trip", "[core]")
{
    using kraftonic::core::toNormalized;
    using kraftonic::core::toPlain;
    for (double n = 0.0; n <= 1.0; n += 0.125)
    {
        CHECK (toNormalized (kGain, toPlain (kGain, n)) == Catch::Approx (n).margin (1e-6));
        CHECK (toNormalized (kTime, toPlain (kTime, n)) == Catch::Approx (n).margin (1e-6));
    }
    CHECK (toPlain (kTime, 0.0) == Catch::Approx (0.001f));
    CHECK (toPlain (kTime, 1.0) == Catch::Approx (10.0f));
    CHECK (toPlain (kGain, -1.0) == Catch::Approx (-60.0f));
    CHECK (toPlain (kGain, 2.0) == Catch::Approx (6.0f));
}

TEST_CASE ("Stepped parameters snap to their grid", "[core]")
{
    using kraftonic::core::clampPlain;
    using kraftonic::core::toNormalized;
    using kraftonic::core::toPlain;
    CHECK (kGain.stepCount == 0);
    CHECK (toPlain (kMode, 0.0) == 0.0f);
    CHECK (toPlain (kMode, 0.3) == 1.0f);
    CHECK (toPlain (kMode, 0.5) == 1.0f);
    CHECK (toPlain (kMode, 0.8) == 2.0f);
    CHECK (toNormalized (kMode, 1.0f) == Catch::Approx (0.5));
    CHECK (toNormalized (kMode, 1.4f) == Catch::Approx (0.5));
    CHECK (clampPlain (kMode, 1.6f) == 2.0f);
    CHECK (clampPlain (kMode, 9.0f) == 2.0f);
}
