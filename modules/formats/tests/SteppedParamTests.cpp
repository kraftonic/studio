// SPDX-License-Identifier: MIT
//
// Stepped-parameter plain-value convention tests.
// SDK-free: checks the step-index helpers the shared Parameter<Traits> and
// the UI selectors both build on, so VSTGUI option menus, segment buttons
// and selectors agree on every product.

#include "kraftonic/formats/SteppedParam.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

using kraftonic::formats::normalizedFromStepIndex;
using kraftonic::formats::stepIndexFromNormalized;

TEST_CASE ("step index snaps the normalized value to its grid", "[stepped]")
{
    // 3-way switch (stepCount 2): thirds of the slider map to 0/1/2.
    CHECK (stepIndexFromNormalized (2, 0.0) == 0.0);
    CHECK (stepIndexFromNormalized (2, 0.24) == 0.0);
    CHECK (stepIndexFromNormalized (2, 0.26) == 1.0);
    CHECK (stepIndexFromNormalized (2, 0.5) == 1.0);
    CHECK (stepIndexFromNormalized (2, 0.76) == 2.0);
    CHECK (stepIndexFromNormalized (2, 1.0) == 2.0);
    // Toggle (stepCount 1) flips at the midpoint.
    CHECK (stepIndexFromNormalized (1, 0.49) == 0.0);
    CHECK (stepIndexFromNormalized (1, 0.5) == 1.0);
    // Out-of-range normalized values clamp to the ends, never past them.
    CHECK (stepIndexFromNormalized (2, -0.5) == 0.0);
    CHECK (stepIndexFromNormalized (2, 1.5) == 2.0);
}

TEST_CASE ("step index converts back to the middle of its cell", "[stepped]")
{
    CHECK (normalizedFromStepIndex (2, 0.0) == 0.0);
    CHECK (normalizedFromStepIndex (2, 1.0) == 0.5);
    CHECK (normalizedFromStepIndex (2, 2.0) == 1.0);
    // Fractional plain values round to the nearest step first.
    CHECK (normalizedFromStepIndex (2, 0.7) == 0.5);
    CHECK (normalizedFromStepIndex (2, 1.6) == 1.0);
    // Out-of-range indices clamp instead of leaving [0, 1].
    CHECK (normalizedFromStepIndex (2, -3.0) == 0.0);
    CHECK (normalizedFromStepIndex (2, 9.0) == 1.0);
}

TEST_CASE ("step round trip lands on the grid", "[stepped]")
{
    for (int steps = 1; steps <= 8; ++steps)
    {
        for (int i = 0; i <= 20; ++i)
        {
            const double normalized = i / 20.0;
            const double index = stepIndexFromNormalized (steps, normalized);
            CHECK (index == Catch::Approx (std::round (index)));
            const double back = normalizedFromStepIndex (steps, index);
            CHECK (back == Catch::Approx (std::round (back * steps) / steps));
        }
    }
}

TEST_CASE ("continuous parameters keep the identity mapping", "[stepped]")
{
    CHECK (stepIndexFromNormalized (0, 0.33) == 0.33);
    CHECK (normalizedFromStepIndex (0, 0.33) == 0.33);
}
