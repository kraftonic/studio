// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <string>

namespace kraftonic::sampler::detail
{
/** UTF-8 string to a filesystem path on every platform (Windows paths are
    UTF-16 internally, so a plain std::string constructor would use the ANSI
    code page). */
inline std::filesystem::path pathFromUtf8 (const std::string& utf8)
{
    std::u8string u8 (utf8.size(), u8'\0');
    for (std::size_t i = 0; i < utf8.size(); ++i)
        u8[i] = static_cast<char8_t> (utf8[i]);
    return std::filesystem::path (u8);
}

inline std::string pathToUtf8 (const std::filesystem::path& path)
{
    const auto u8 = path.u8string();
    std::string out (u8.size(), '\0');
    for (std::size_t i = 0; i < u8.size(); ++i)
        out[i] = static_cast<char> (u8[i]);
    return out;
}
} // namespace kraftonic::sampler::detail
