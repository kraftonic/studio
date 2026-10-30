// SPDX-License-Identifier: MIT
//
// Shared Parameter<Traits> plain-value tests.
// Built only with KRAFTONIC_BUILD_PLUGINS: drives the real VST3 Parameter
// subclass with the SelfTest traits, with no host.
//
// Regression test: stepped parameters must expose
// plain values as step indices (0..stepCount, the SDK StringListParameter
// convention), so VSTGUI option menus, segment buttons and stepped sliders
// show the right entry. Continuous parameters keep the identity mapping,
// and display strings still come from the product's engine-unit hook.

#include "SelfTestTraits.h"
#include "kraftonic/formats/DisplayText.h"
#include "kraftonic/formats/vst3/Vst3Controller.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstring>

using kraftonic::formats::selftest::ParamId;
using kraftonic::formats::selftest::SelfTestTraits;

namespace
{

template <typename Traits>
Steinberg::Vst::Parameter* makeParameter (typename Traits::ParamId id)
{
    return new kraftonic::formats::vst3::Parameter<Traits> (Traits::paramInfo (id));
}

} // namespace

TEST_CASE ("stepped parameters expose step indices as plain values", "[stepped][vst3]")
{
    Steinberg::IPtr<Steinberg::Vst::Parameter> wave {makeParameter<SelfTestTraits> (ParamId::wave)};
    REQUIRE (wave->getInfo().stepCount == 2);
    CHECK (wave->toPlain (0.0) == 0.0);
    CHECK (wave->toPlain (0.24) == 0.0);
    CHECK (wave->toPlain (0.26) == 1.0);
    CHECK (wave->toPlain (0.5) == 1.0);
    CHECK (wave->toPlain (0.76) == 2.0);
    CHECK (wave->toPlain (1.0) == 2.0);
    CHECK (wave->toNormalized (0.0) == 0.0);
    CHECK (wave->toNormalized (1.0) == 0.5);
    CHECK (wave->toNormalized (2.0) == 1.0);
    // Out-of-range plain values clamp instead of leaving [0, 1].
    CHECK (wave->toNormalized (-1.0) == 0.0);
    CHECK (wave->toNormalized (7.0) == 1.0);
}

TEST_CASE ("continuous parameters keep plain == normalized", "[stepped][vst3]")
{
    Steinberg::IPtr<Steinberg::Vst::Parameter> gain {
        makeParameter<SelfTestTraits> (ParamId::outputGain)};
    REQUIRE (gain->getInfo().stepCount == 0);
    CHECK (gain->toPlain (0.33) == Catch::Approx (0.33));
    CHECK (gain->toNormalized (0.33) == Catch::Approx (0.33));
}

TEST_CASE ("bypass exposes its two steps as plain 0/1", "[stepped][vst3]")
{
    Steinberg::IPtr<Steinberg::Vst::Parameter> bypass {
        new kraftonic::formats::vst3::BypassParameter (9999)};
    REQUIRE (bypass->getInfo().stepCount == 1);
    CHECK (bypass->toPlain (0.0) == 0.0);
    CHECK (bypass->toPlain (0.7) == 1.0);
    CHECK (bypass->toNormalized (0.0) == 0.0);
    CHECK (bypass->toNormalized (1.0) == 1.0);
}

TEST_CASE ("display strings still use engine units, not step indices", "[stepped][vst3]")
{
    Steinberg::IPtr<Steinberg::Vst::Parameter> wave {makeParameter<SelfTestTraits> (ParamId::wave)};
    Steinberg::Vst::String128 text {};
    char utf8[128] {};
    wave->toString (0.0, text);
    REQUIRE (kraftonic::formats::decodeDisplayText (text, utf8, sizeof (utf8)));
    CHECK (std::strcmp (utf8, "Sine") == 0);
    wave->toString (0.5, text);
    REQUIRE (kraftonic::formats::decodeDisplayText (text, utf8, sizeof (utf8)));
    CHECK (std::strcmp (utf8, "Square") == 0);
    wave->toString (1.0, text);
    REQUIRE (kraftonic::formats::decodeDisplayText (text, utf8, sizeof (utf8)));
    CHECK (std::strcmp (utf8, "Triangle") == 0);

    // fromString parses engine-unit text back to the stepped normalized value.
    Steinberg::Vst::ParamValue normalized = -1.0;
    std::memset (text, 0, sizeof (text));
    Steinberg::UString (text, 128).assign ("Square");
    REQUIRE (wave->fromString (text, normalized));
    CHECK (normalized == Catch::Approx (0.5));
}
