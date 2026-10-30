// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic sampler: where installed sample data lives (NOT real-time).
//
//  A product's sample root, in order of precedence:
//    1. its environment variable (e.g. MYSYNTH_SAMPLES), if set and non-empty;
//    2. the platform default for <Vendor>/<Product>:
//         macOS    /Library/Application Support/<Vendor>/<Product>/Samples
//         Windows  %PROGRAMDATA%\<Vendor>\<Product>\Samples
//         Linux    $HOME/.local/share/<vendor>/<product>/samples (lower case)
//  Each instrument is a sub-folder holding an instrument.ini, see README.md.
// ============================================================================

#include <string>

namespace kraftonic::sampler
{
/** The platform default for a product, ignoring any environment override. UTF-8. */
std::string platformDefaultSampleDirectory (const std::string& vendor, const std::string& product);

/** The folder a product should load from: the environment variable if set, else the default. UTF-8. */
std::string sampleDirectory (const std::string& vendor, const std::string& product, const char* environmentVariable);

/** `root` joined with an instrument folder name, e.g. "grand". UTF-8. */
std::string instrumentDirectory (const std::string& rootUtf8, const std::string& instrument);
} // namespace kraftonic::sampler
