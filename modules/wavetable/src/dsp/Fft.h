// SPDX-License-Identifier: MIT
#pragma once

#include <complex>
#include <vector>

namespace kraftonic::wavetable::detail
{
/** Radix-2 complex FFT for table generation and tests (not real-time).
    Twiddles are computed once for `maxSize`; any power-of-two size up to it
    can then be transformed. inverse = true uses e^{+i...} and does not scale. */
class Fft
{
public:
    explicit Fft (std::size_t maxSize);
    void transform (std::vector<std::complex<double>>& data, bool inverse) const;

private:
    std::size_t maxSize;
    std::vector<double> cosTable, sinTable; // e^{-i 2 pi k / maxSize}, k < maxSize / 2
};

/** Convenience one-shot transform (builds its own twiddles). */
void fft (std::vector<std::complex<double>>& data, bool inverse);
} // namespace kraftonic::wavetable::detail
