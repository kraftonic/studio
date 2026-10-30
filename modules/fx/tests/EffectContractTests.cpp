// SPDX-License-Identifier: MIT
#include "kraftonic/fx/Effect.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
// Minimal reference shape for the contract. Real effects live in
// modules/fx/include/kraftonic/fx/ and get their own test files.
class PassThrough
{
public:
    struct Parameters
    {
        bool enabled = true;
    };

    void prepare (const kraftonic::fx::ProcessSetup&) {}
    void reset() noexcept {}
    void setParameters (const Parameters&) noexcept {}
    void process (float*, float*, int) noexcept {}
    int latencySamples() const noexcept { return 0; }
};

struct NotAnEffect
{
};
} // namespace

TEST_CASE ("The effect contract concept accepts the reference shape", "[fx]")
{
    STATIC_CHECK (kraftonic::fx::StereoEffect<PassThrough>);
    STATIC_CHECK_FALSE (kraftonic::fx::StereoEffect<NotAnEffect>);
}
