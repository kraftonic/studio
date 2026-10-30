// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic plugin — CLAP entry point.
//
//  A product's entry file (next to KRAFTONIC_VST3_FACTORY) adds one line:
//
//      #include <kraftonic/formats/clap/ClapEntry.h>
//      KRAFTONIC_CLAP_ENTRY (MyTraits, FULL_VERSION_STR,
//                            CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
//                            CLAP_PLUGIN_FEATURE_STEREO)
//
//  kraftonic_add_plugin compiles the same sources a second time as the .clap,
//  with KRAFTONIC_CLAP defined and KRAFTONIC_CLAP_ID set to the plug-in's
//  BUNDLE_IDENTIFIER (the CLAP plug-in id: permanent once shipped, like the
//  VST3 class ids). In the VST3 build the macro is empty. Name, vendor and
//  URL come from the traits, as in the VST3 factory; the features are the
//  CLAP_PLUGIN_FEATURE_* strings (a main category first).
// ============================================================================

#if defined(KRAFTONIC_CLAP)

#include "kraftonic/formats/clap/ClapPlugin.h"

#include <clap/clap.h>

#include <cstring>
#include <mutex>

#if KRAFTONIC_CLAP_GUI
#include "kraftonic/formats/clap/ClapEditor.h"
#define KRAFTONIC_CLAP_EDITOR ::kraftonic::formats::clap::Editor
#else
#define KRAFTONIC_CLAP_EDITOR ::kraftonic::formats::clap::NoEditor
#endif

#if !defined(KRAFTONIC_CLAP_ID)
#error "KRAFTONIC_CLAP_ID (the plug-in's bundle identifier) must be defined for CLAP builds"
#endif

namespace kraftonic::formats::clap
{

/** The module's one-plug-in factory. Identity: a struct with TraitsType,
    kId, kVersion and kFeatures (see KRAFTONIC_CLAP_ENTRY). */
template <typename Identity, template <typename> class EditorT>
struct Entry
{
    using Traits = typename Identity::TraitsType;
    using PluginType = Plugin<Traits, EditorT>;

    static inline const clap_plugin_descriptor descriptor {
        CLAP_VERSION_INIT,
        Identity::kId,
        Traits::kPluginName,
        Traits::kVendor,
        Traits::kVendorUrl,
        "",
        "",
        Identity::kVersion,
        "",
        Identity::kFeatures,
    };

    static inline const clap_plugin_factory factory {
        [] (const clap_plugin_factory*) -> std::uint32_t { return 1; },
        [] (const clap_plugin_factory*, std::uint32_t index) -> const clap_plugin_descriptor* {
            return index == 0 ? &descriptor : nullptr;
        },
        [] (const clap_plugin_factory*, const clap_host* host, const char* id) -> const clap_plugin* {
            if (host == nullptr || id == nullptr || std::strcmp (id, descriptor.id) != 0
                || !clap_version_is_compatible (host->clap_version))
            {
                return nullptr;
            }
            return (new PluginType (host, &descriptor))->clapPlugin();
        },
    };

    // init/deinit may come in matched pairs more than once: count them.
    static inline std::mutex mutex;
    static inline int initCount = 0;

    static bool init (const char* pluginPath)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        if (initCount == 0 && !PluginType::EditorType::initModule (pluginPath))
            return false;
        ++initCount;
        return true;
    }

    static void deinit()
    {
        const std::lock_guard<std::mutex> lock (mutex);
        if (initCount > 0 && --initCount == 0)
            PluginType::EditorType::deinitModule();
    }

    static const void* getFactory (const char* id)
    {
        return id != nullptr && std::strcmp (id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
    }

    static constexpr clap_plugin_entry entry {CLAP_VERSION_INIT, init, deinit, getFactory};
};

} // namespace kraftonic::formats::clap

#define KRAFTONIC_CLAP_ENTRY(Traits, VersionString, ...) \
    namespace kraftonic_clap_entry \
    { \
    struct Identity \
    { \
        using TraitsType = Traits; \
        static constexpr const char* kId = KRAFTONIC_CLAP_ID; \
        static constexpr const char* kVersion = VersionString; \
        static constexpr const char* kFeatures[] = {__VA_ARGS__, nullptr}; \
    }; \
    } \
    extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry \
        = ::kraftonic::formats::clap::Entry<kraftonic_clap_entry::Identity, KRAFTONIC_CLAP_EDITOR>::entry;

#else

#define KRAFTONIC_CLAP_ENTRY(Traits, VersionString, ...)

#endif
