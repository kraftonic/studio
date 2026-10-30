// SPDX-License-Identifier: MIT
#include <kraftonic/core/Smoother.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

TEST_CASE ("LinearSmoother ramps to its target in the configured time", "[smoother]")
{
    kraftonic::core::LinearSmoother s;
    s.prepare (1000.0, 0.01); // 10 samples
    s.setImmediate (0.0f);
    s.setTarget (1.0f);

    float prev = 0.0f;
    for (int i = 0; i < 10; ++i)
    {
        const float v = s.next();
        REQUIRE (v > prev);
        REQUIRE_THAT (v - prev, WithinAbs (0.1, 1e-5));
        prev = v;
    }
    REQUIRE (s.getCurrent() == 1.0f);
    REQUIRE_FALSE (s.isSmoothing());
}
