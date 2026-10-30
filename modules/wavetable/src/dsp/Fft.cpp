// SPDX-License-Identifier: MIT
#include "Fft.h"

#include "kraftonic/core/Math.h"

#include <cmath>
#include <utility>

namespace kraftonic::wavetable::detail
{
Fft::Fft (std::size_t size) : maxSize (size)
{
    cosTable.resize (size / 2);
    sinTable.resize (size / 2);
    for (std::size_t k = 0; k < size / 2; ++k)
    {
        const double angle = core::kTwoPi * static_cast<double> (k) / static_cast<double> (size);
        cosTable[k] = std::cos (angle);
        sinTable[k] = -std::sin (angle);
    }
}

void Fft::transform (std::vector<std::complex<double>>& data, bool inverse) const
{
    const std::size_t n = data.size();
    // Plain real arithmetic: std::complex multiplication is slow without fast-math.
    auto* d = reinterpret_cast<double*> (data.data());
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (data[i], data[j]);
    }
    const double sign = inverse ? -1.0 : 1.0;
    for (std::size_t length = 2; length <= n; length <<= 1)
    {
        const std::size_t half = length / 2;
        const std::size_t stride = maxSize / length;
        for (std::size_t start = 0; start < n; start += length)
            for (std::size_t k = 0; k < half; ++k)
            {
                const double wr = cosTable[k * stride];
                const double wi = sign * sinTable[k * stride];
                double* a = d + 2 * (start + k);
                double* b = d + 2 * (start + k + half);
                const double br = b[0] * wr - b[1] * wi;
                const double bi = b[0] * wi + b[1] * wr;
                b[0] = a[0] - br;
                b[1] = a[1] - bi;
                a[0] += br;
                a[1] += bi;
            }
    }
}

void fft (std::vector<std::complex<double>>& data, bool inverse)
{
    Fft (data.size()).transform (data, inverse);
}
} // namespace kraftonic::wavetable::detail
