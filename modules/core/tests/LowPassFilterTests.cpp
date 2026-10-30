// SPDX-License-Identifier: MIT
#include <kraftonic/core/LowPassFilter.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using kraftonic::core::LowPassFilter;

TEST_CASE ("Low-pass filter stays finite and bounded across its operating range", "[filter]")
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (float resonance : { 0.0f, 0.5f, 1.0f })
            for (double cutoff : { 20.0, 200.0, 20000.0 })
            {
                LowPassFilter filter;
                filter.prepare (sr);
                double peak = 0.0;
                for (int i = 0; i < 32768; ++i)
                {
                    const float input = i == 0 ? 1.0f : 0.0f;
                    const float output = filter.process (input, cutoff, resonance);
                    REQUIRE (std::isfinite (output));
                    peak = std::max (peak, std::abs (static_cast<double> (output)));
                }
                REQUIRE (peak < 4.0);
            }
}

TEST_CASE ("Low-pass impulse response decays below full resonance", "[filter]")
{
    for (double sr : { 44100.0, 192000.0 })
        for (float resonance : { 0.0f, 0.5f, 0.9f })
        {
            LowPassFilter filter;
            filter.prepare (sr);
            filter.process (1.0f, 1000.0, resonance);
            double tailPeak = 0.0;
            const int count = static_cast<int> (sr * 0.2);
            for (int i = 0; i < count; ++i)
            {
                const double output = filter.process (0.0f, 1000.0, resonance);
                if (i > count - 1000)
                    tailPeak = std::max (tailPeak, std::abs (output));
            }
            REQUIRE (tailPeak < 0.0001);
        }
}
