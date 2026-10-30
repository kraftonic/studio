// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic plugin — VST3 display-text codec.
//
//  Host parameter strings (VST3 String128) are UTF-16; Kraftonic display text
//  is UTF-8, and the only non-ASCII codepoint ever emitted is U+2212 MINUS
//  (so negative values read as "−8.0 dB"). These helpers bridge the two.
//  Decoding accepts only ASCII and U+2212 so hosts can never smuggle
//  unexpected text into the value parser. SDK-free so it unit-tests without
//  a host. Always NUL-terminates (truncates on overflow without splitting a
//  UTF-8 sequence).
// ============================================================================

#include <cstddef>
#include <cstdint>

namespace kraftonic::formats
{

/** Encodes UTF-8 display text into a UTF-16 buffer of `capacity` units. */
inline void encodeDisplayText (const char* utf8, char16_t* utf16, std::size_t capacity)
{
    if (utf16 == nullptr || capacity == 0)
        return;
    std::size_t slot = 0;
    if (utf8 != nullptr)
    {
        const auto* p = reinterpret_cast<const unsigned char*> (utf8);
        while (*p != '\0' && slot + 1 < capacity)
        {
            char32_t codepoint = 0;
            std::size_t extra = 0;
            if (*p < 0x80)
            {
                codepoint = *p;
                extra = 0;
            }
            else if ((*p & 0xE0) == 0xC0)
            {
                codepoint = static_cast<char32_t> (*p & 0x1Fu);
                extra = 1;
            }
            else if ((*p & 0xF0) == 0xE0)
            {
                codepoint = static_cast<char32_t> (*p & 0x0Fu);
                extra = 2;
            }
            else
            {
                codepoint = 0xFFFDu; // 4-byte sequences cannot occur in String128
                extra = 0;
                while ((p[1] & 0xC0) == 0x80)
                    ++p;
            }
            for (std::size_t k = 0; k < extra; ++k)
            {
                if ((p[1] & 0xC0) != 0x80)
                {
                    codepoint = 0xFFFDu;
                    extra = k;
                    break;
                }
                codepoint = (codepoint << 6) | static_cast<char32_t> (p[1] & 0x3Fu);
                ++p;
            }
            utf16[slot++] = static_cast<char16_t> (codepoint);
            ++p;
        }
    }
    utf16[slot] = 0;
}

/**
 * Decodes a UTF-16 string into UTF-8 for parsing. Returns false (leaving
 * `utf8` untouched apart from termination guarantees) on null input,
 * unsupported codepoints, or overflow.
 */
inline bool decodeDisplayText (const char16_t* in, char* utf8, std::size_t size)
{
    if (in == nullptr || utf8 == nullptr || size == 0)
        return false;
    std::size_t count = 0;
    for (const char16_t* p = in; *p != 0; ++p)
    {
        const char32_t c = static_cast<char32_t> (*p);
        if (c < 0x80)
        {
            if (count + 1 >= size)
                return false;
            utf8[count++] = static_cast<char> (c);
        }
        else if (c == 0x2212)
        {
            if (count + 3 >= size)
                return false;
            utf8[count++] = '\xE2';
            utf8[count++] = '\x88';
            utf8[count++] = '\x92';
        }
        else
        {
            return false;
        }
    }
    if (count + 1 > size)
        return false;
    utf8[count] = '\0';
    return true;
}

} // namespace kraftonic::formats
