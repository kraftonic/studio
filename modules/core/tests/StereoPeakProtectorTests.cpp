// SPDX-License-Identifier: MIT
#include <kraftonic/core/StereoPeakProtector.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using kraftonic::core::StereoPeakProtector;

TEST_CASE ("Stereo peak protection is transparent below minus one dBFS", "[core]")
{
    for (float input : { -0.8f, -0.5f, 0.0f, 0.4f, 0.8f })
    {
        float left = input;
        float right = -0.7f * input;
        StereoPeakProtector::process (left, right);
        REQUIRE (left == input);
        REQUIRE (right == -0.7f * input);
    }
}

TEST_CASE ("Stereo peak protection is smooth and stereo linked", "[core]")
{
    float previous = StereoPeakProtector::kThreshold;
    for (int i = 1; i <= 200; ++i)
    {
        const float input = StereoPeakProtector::kThreshold + static_cast<float> (i) * 0.01f;
        float left = input;
        float right = -0.5f * input;
        StereoPeakProtector::process (left, right);
        REQUIRE (std::isfinite (left));
        REQUIRE (left > previous);
        REQUIRE (left < 1.0f);
        REQUIRE (std::abs (right + 0.5f * left) < 1.0e-6f);
        previous = left;
    }
}
