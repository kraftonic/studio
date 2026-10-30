// SPDX-License-Identifier: MIT
#include "kraftonic/sampler/SampleLocations.h"

#include "PathUtil.h"

#include <cctype>
#include <cstdlib>

namespace kraftonic::sampler
{
namespace
{
std::string environment (const char* name)
{
#if defined(_MSC_VER)
    // getenv is flagged as unsafe by MSVC (C4996); _dupenv_s is the sanctioned form.
    char* value = nullptr;
    std::size_t length = 0;
    std::string result;
    if (_dupenv_s (&value, &length, name) == 0 && value != nullptr)
        result = value;
    std::free (value);
    return result;
#else
    const char* value = std::getenv (name);
    return value != nullptr ? std::string (value) : std::string();
#endif
}
} // namespace

std::string platformDefaultSampleDirectory (const std::string& vendor, const std::string& product)
{
#if defined(_WIN32)
    std::string base = environment ("PROGRAMDATA");
    if (base.empty())
        base = "C:\\ProgramData";
    return base + "\\" + vendor + "\\" + product + "\\Samples";
#elif defined(__APPLE__)
    return "/Library/Application Support/" + vendor + "/" + product + "/Samples";
#else
    auto lower = [] (std::string text)
    {
        for (auto& c : text)
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return text;
    };
    const std::string relative = lower (vendor) + "/" + lower (product) + "/samples";
    std::string home = environment ("HOME");
    if (home.empty())
        return relative; // no home: relative to the working directory
    return home + "/.local/share/" + relative;
#endif
}

std::string sampleDirectory (const std::string& vendor, const std::string& product, const char* environmentVariable)
{
    const std::string overridden = environmentVariable != nullptr ? environment (environmentVariable) : std::string();
    return overridden.empty() ? platformDefaultSampleDirectory (vendor, product) : overridden;
}

std::string instrumentDirectory (const std::string& rootUtf8, const std::string& instrument)
{
    return detail::pathToUtf8 (detail::pathFromUtf8 (rootUtf8) / detail::pathFromUtf8 (instrument));
}
} // namespace kraftonic::sampler
