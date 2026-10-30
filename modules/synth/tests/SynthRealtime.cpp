// SPDX-License-Identifier: MIT
#include "kraftonic/synth/Filters.h"
#include "kraftonic/synth/Modulation.h"
#include "kraftonic/synth/Noise.h"
#include "kraftonic/synth/Oscillator.h"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
int main ()
{
    using namespace kraftonic::synth;
    Oscillator o;
    o.prepare (48000);
    o.setFrequency (440);
    Noise n;
    LadderFilter f;
    f.prepare (48000);
    StateVariableFilter s;
    s.prepare (48000);
    Lfo l;
    l.prepare (48000);
    std::array<float, 512> a{};
    double sum = 0;
    auto t = std::chrono::steady_clock::now ();
    for (int b = 0; b < 1000; ++b)
        for (auto &x : a)
        {
            x = o.process (Waveform::saw) + .1f * n.white ();
            x = f.process (x);
            x = s.process (x, SVFMode::lowPass);
            x += .1f * l.process (LfoWave::triangle);
            sum += x;
        }
    auto ns = std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now () - t).count ();
    std::printf ("synth: %.1f ns/sample/voice, finite=%d\n", ns / (512000), std::isfinite (sum));
    return std::isfinite (sum) ? 0 : 1;
}
