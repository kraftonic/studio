// SPDX-License-Identifier: MIT
//
// DisplayText tests. Pins the UTF-8/UTF-16 bridge the
// controller uses for host parameter strings: ASCII round-trips, U+2212
// survives both directions, anything else is rejected, and truncation never
// splits a sequence or overruns.

#include "kraftonic/formats/DisplayText.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace kraftonic::formats;

TEST_CASE ("display text round-trips ASCII", "[plugin-text]")
{
    char16_t wide[128] {};
    encodeDisplayText ("-8.0 dB", wide, 128);
    CHECK (wide[0] == u'-');
    CHECK (wide[6] == u'B');

    char text[128] {};
    CHECK (decodeDisplayText (wide, text, sizeof (text)));
    CHECK (std::string (text) == "-8.0 dB");
}

TEST_CASE ("display text keeps the unicode minus", "[plugin-text]")
{
    char16_t wide[128] {};
    encodeDisplayText ("−8.0 dB", wide, 128); // U+2212
    CHECK (wide[0] == u'−');

    char text[128] {};
    CHECK (decodeDisplayText (wide, text, sizeof (text)));
    CHECK (std::string (text) == "−8.0 dB");
}

TEST_CASE ("display text rejects anything unexpected", "[plugin-text]")
{
    char text[128] = {};
    const char16_t snowman[] = {u'5', 0x2603, 0}; // U+2603 not allowed
    CHECK (!decodeDisplayText (snowman, text, sizeof (text)));
    CHECK (!decodeDisplayText (nullptr, text, sizeof (text)));
    CHECK (!decodeDisplayText (snowman, nullptr, 0));

    char16_t wide[128] {};
    encodeDisplayText (nullptr, wide, 128);
    CHECK (wide[0] == 0);
}

TEST_CASE ("display text truncates safely", "[plugin-text]")
{
    char16_t wide[4] {};
    encodeDisplayText ("abcdef", wide, 4);
    CHECK (wide[3] == 0);

    const char16_t in[] = {u'a', u'b', u'c', u'd', 0};
    char small[3] {};
    CHECK (!decodeDisplayText (in, small, sizeof (small)));
}
