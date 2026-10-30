// SPDX-License-Identifier: MIT
//
// Unit tests for kraftonic::formats::OutputMeterFeed.

#include "kraftonic/formats/OutputMeter.h"

#include <catch2/catch_test_macros.hpp>

using kraftonic::formats::OutputMeterFeed;

TEST_CASE ("meter feed ticks at ~30 Hz", "[output-meter]")
{
    OutputMeterFeed feed {};
    feed.setSampleRate (48000.0);
    CHECK (feed.intervalSamples() == 1600);
    CHECK_FALSE (feed.ready());
    feed.observe (0.5F, 0.4F, 1599);
    CHECK_FALSE (feed.ready());
    feed.observe (0.1F, 0.1F, 1);
    CHECK (feed.ready());
    float left = 0.0F;
    float right = 0.0F;
    feed.take (left, right);
    CHECK (left == 0.5F);
    CHECK (right == 0.4F);
    CHECK_FALSE (feed.ready()); // tick restarted
}

TEST_CASE ("meter feed keeps block maxima and decays through silence", "[output-meter]")
{
    OutputMeterFeed feed {};
    feed.setSampleRate (44100.0);
    CHECK (feed.intervalSamples() == 1470);
    feed.observe (0.2F, 0.9F, 1000);
    feed.observe (0.8F, 0.1F, 1000); // later blocks only raise the peaks
    float left = 0.0F;
    float right = 0.0F;
    feed.take (left, right);
    CHECK (left == 0.8F);
    CHECK (right == 0.9F);
    // A silent tick emits zeros, so held meters fall on their own.
    feed.observe (0.0F, 0.0F, 2000);
    feed.take (left, right);
    CHECK (left == 0.0F);
    CHECK (right == 0.0F);
}

TEST_CASE ("meter feed clamps take() to 0..1", "[output-meter]")
{
    OutputMeterFeed feed {};
    feed.setSampleRate (48000.0);
    feed.observe (2.5F, 0.25F, 1600);
    float left = 0.0F;
    float right = 0.0F;
    feed.take (left, right);
    CHECK (left == 1.0F);
    CHECK (right == 0.25F);
}
