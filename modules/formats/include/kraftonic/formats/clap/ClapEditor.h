// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Kraftonic plugin — the CLAP editor: the product's VST3 panel in a CLAP host.
//
//  The panel is the same VSTGUI editor the VST3 plug-in shows (the generated
//  .uidesc, kit bitmaps and fonts, any subcontrollers), built by the
//  product's own VST3 controller, which the .clap also contains: the
//  controller is created from the module's VST3 factory and serves only as
//  the panel's model. A small bridge connects it to the CLAP plug-in:
//
//    panel -> plug-in   IComponentHandler begin/perform/endEdit become
//                       Plugin::editFromUi (lock-free, the audio thread
//                       tells the host); a preset picked on the panel
//                       (restartComponent) sends every changed value;
//    plug-in -> panel   a ~30 Hz UI-thread timer copies changed values
//                       (host automation, state loads), output meters and
//                       held notes into the controller;
//    resizing           IPlugFrame::resizeView asks the host (gui.request_resize).
//
//  macOS only for now (cocoa): VSTGUI finds the panel files in the .clap
//  bundle's Contents/Resources. Windows and Linux .clap files are single
//  shared libraries with no resource folder, so they use NoEditor and hosts
//  show generic controls (see modules/formats/README.md).
//
//  Main thread only, except where noted.
// ============================================================================

#include "kraftonic/formats/clap/ClapPlugin.h"
#include "kraftonic/formats/vst3/Vst3SdkIncludes.h"

#include <CoreFoundation/CoreFoundation.h>

#include <array>
#include <cstring>

extern "C" {
// The VST3 SDK's macOS module entry (macmain.cpp, linked into the .clap):
// sets the module's bundle and runs the module initializers (VSTGUI init).
bool bundleEntry (CFBundleRef);
bool bundleExit (void);
}

namespace kraftonic::formats::clap
{

template <typename PluginT>
class Editor
{
public:
    static constexpr bool kEnabled = true;
    using Traits = typename PluginT::TraitsType;
    using ParamId = typename Traits::ParamId;
    static constexpr int kNumParams = Traits::kNumParams;

    explicit Editor (PluginT& plugin) : plugin_ (plugin) {}

    ~Editor()
    {
        if (timer_)
            timer_->stop();
        timer_ = nullptr;
        if (view_)
        {
            if (attached_)
                view_->removed();
            view_->setFrame (nullptr);
            view_ = nullptr; // the VST3Editor goes now, before its controller
        }
        if (controller_)
        {
            if (Traits::numPresets() > 0)
                plugin_.uiProgram_
                    = controller_->getParamNormalized (static_cast<Steinberg::Vst::ParamID> (Traits::kProgramParamId));
            controller_->setComponentHandler (nullptr);
            controller_->terminate();
            controller_ = nullptr;
        }
        if (bridge_)
            bridge_->editor = nullptr;
    }

    Editor (const Editor&) = delete;
    Editor& operator= (const Editor&) = delete;

    /** clap_entry.init: VSTGUI and the bundle's resources. */
    static bool initModule (const char* pluginPath) noexcept
    {
        if (pluginPath == nullptr)
            return false;
        CFURLRef url = CFURLCreateFromFileSystemRepresentation (nullptr, reinterpret_cast<const UInt8*> (pluginPath),
                                                               static_cast<CFIndex> (std::strlen (pluginPath)), true);
        if (url == nullptr)
            return false;
        CFBundleRef bundle = CFBundleCreate (nullptr, url);
        CFRelease (url);
        if (bundle == nullptr)
            return false;
        const bool ok = bundleEntry (bundle); // retains the bundle
        CFRelease (bundle);
        return ok;
    }

    static void deinitModule() noexcept { bundleExit(); }

    static const clap_plugin_gui* guiExtension() noexcept
    {
        static const clap_plugin_gui extension {
            [] (const clap_plugin*, const char* api, bool isFloating) { return isSupported (api, isFloating); },
            [] (const clap_plugin*, const char** api, bool* isFloating) {
                if (api == nullptr || isFloating == nullptr)
                    return false;
                *api = CLAP_WINDOW_API_COCOA;
                *isFloating = false;
                return true;
            },
            [] (const clap_plugin* p, const char* api, bool isFloating) {
                PluginT& plugin = self (p);
                if (!isSupported (api, isFloating) || plugin.editor_ != nullptr)
                    return false;
                plugin.editor_ = std::make_unique<Editor> (plugin);
                if (!plugin.editor_->create())
                {
                    plugin.editor_.reset();
                    return false;
                }
                return true;
            },
            [] (const clap_plugin* p) { self (p).editor_.reset(); },
            [] (const clap_plugin*, double) { return false; }, // macOS scales by itself
            [] (const clap_plugin* p, std::uint32_t* width, std::uint32_t* height) {
                Editor* editor = self (p).editor_.get();
                return editor != nullptr && width != nullptr && height != nullptr && editor->size (*width, *height);
            },
            [] (const clap_plugin* p) {
                Editor* editor = self (p).editor_.get();
                return editor != nullptr && editor->view_->canResize() == Steinberg::kResultTrue;
            },
            [] (const clap_plugin* p, clap_gui_resize_hints* hints) {
                Editor* editor = self (p).editor_.get();
                if (editor == nullptr || hints == nullptr || editor->view_->canResize() != Steinberg::kResultTrue)
                    return false;
                hints->can_resize_horizontally = true;
                hints->can_resize_vertically = true;
                hints->preserve_aspect_ratio = false;
                hints->aspect_ratio_width = 1;
                hints->aspect_ratio_height = 1;
                return true;
            },
            [] (const clap_plugin* p, std::uint32_t* width, std::uint32_t* height) {
                Editor* editor = self (p).editor_.get();
                if (editor == nullptr || width == nullptr || height == nullptr
                    || editor->view_->canResize() != Steinberg::kResultTrue)
                {
                    return false;
                }
                Steinberg::ViewRect rect (0, 0, static_cast<Steinberg::int32> (*width),
                                          static_cast<Steinberg::int32> (*height));
                if (editor->view_->checkSizeConstraint (&rect) != Steinberg::kResultTrue)
                    return false;
                *width = static_cast<std::uint32_t> (rect.getWidth());
                *height = static_cast<std::uint32_t> (rect.getHeight());
                return true;
            },
            [] (const clap_plugin* p, std::uint32_t width, std::uint32_t height) {
                Editor* editor = self (p).editor_.get();
                if (editor == nullptr)
                    return false;
                Steinberg::ViewRect rect (0, 0, static_cast<Steinberg::int32> (width),
                                          static_cast<Steinberg::int32> (height));
                return editor->view_->onSize (&rect) == Steinberg::kResultTrue;
            },
            [] (const clap_plugin* p, const clap_window* window) {
                Editor* editor = self (p).editor_.get();
                return editor != nullptr && window != nullptr && editor->attach (window->cocoa);
            },
            [] (const clap_plugin*, const clap_window*) { return false; },
            [] (const clap_plugin*, const char*) {},
            [] (const clap_plugin* p) { return self (p).editor_ != nullptr; },
            [] (const clap_plugin* p) { return self (p).editor_ != nullptr; },
        };
        return &extension;
    }

private:
    /** The controller's component handler and the view's frame. */
    class Bridge : public Steinberg::FObject,
                   public Steinberg::Vst::IComponentHandler,
                   public Steinberg::IPlugFrame
    {
    public:
        Editor* editor = nullptr;

        Steinberg::tresult PLUGIN_API beginEdit (Steinberg::Vst::ParamID id) SMTG_OVERRIDE
        {
            if (editor != nullptr)
                editor->plugin_.editFromUi (id, 0.0, EditKind::begin);
            return Steinberg::kResultOk;
        }

        Steinberg::tresult PLUGIN_API performEdit (Steinberg::Vst::ParamID id,
                                                   Steinberg::Vst::ParamValue normalized) SMTG_OVERRIDE
        {
            if (editor != nullptr)
                editor->edited (id, normalized);
            return Steinberg::kResultOk;
        }

        Steinberg::tresult PLUGIN_API endEdit (Steinberg::Vst::ParamID id) SMTG_OVERRIDE
        {
            if (editor != nullptr)
                editor->plugin_.editFromUi (id, 0.0, EditKind::end);
            return Steinberg::kResultOk;
        }

        Steinberg::tresult PLUGIN_API restartComponent (Steinberg::int32 flags) SMTG_OVERRIDE
        {
            if (editor != nullptr && (flags & Steinberg::Vst::kParamValuesChanged) != 0)
                editor->pushControllerValues();
            return Steinberg::kResultOk;
        }

        Steinberg::tresult PLUGIN_API resizeView (Steinberg::IPlugView* view, Steinberg::ViewRect* rect) SMTG_OVERRIDE
        {
            if (editor == nullptr || view == nullptr || rect == nullptr)
                return Steinberg::kInvalidArgument;
            const auto* hostGui = static_cast<const clap_host_gui*> (editor->hostExtension (CLAP_EXT_GUI));
            if (hostGui == nullptr
                || !hostGui->request_resize (editor->plugin_.host(), static_cast<std::uint32_t> (rect->getWidth()),
                                             static_cast<std::uint32_t> (rect->getHeight())))
            {
                return Steinberg::kResultFalse;
            }
            return view->onSize (rect);
        }

        OBJ_METHODS (Bridge, Steinberg::FObject)
        DEFINE_INTERFACES
            DEF_INTERFACE (Steinberg::Vst::IComponentHandler)
            DEF_INTERFACE (Steinberg::IPlugFrame)
        END_DEFINE_INTERFACES (Steinberg::FObject)
        REFCOUNT_METHODS (Steinberg::FObject)
    };

    static PluginT& self (const clap_plugin* p) noexcept { return *static_cast<PluginT*> (p->plugin_data); }

    static bool isSupported (const char* api, bool isFloating) noexcept
    {
        return api != nullptr && !isFloating && std::strcmp (api, CLAP_WINDOW_API_COCOA) == 0;
    }

    const void* hostExtension (const char* id) const noexcept
    {
        const clap_host* host = plugin_.host();
        return host != nullptr && host->get_extension != nullptr ? host->get_extension (host, id) : nullptr;
    }

    // Contract ids are dense (0..kNumParams-1, see Vst3Traits.h), so an id
    // also indexes lastPlain_.
    static ParamId idAt (int index) noexcept { return Traits::paramTable()[index].id; }
    static Steinberg::Vst::ParamID tag (ParamId id) noexcept { return static_cast<Steinberg::Vst::ParamID> (id); }
    static std::size_t slot (ParamId id) noexcept { return static_cast<std::size_t> (id); }

    /** The product's VST3 controller (from the module's factory) and its view. */
    bool create()
    {
        using namespace Steinberg;
        bridge_ = owned (new Bridge());
        bridge_->editor = this;
        auto factory = owned (GetPluginFactory());
        if (!factory)
            return false;
        const FUID cid (Traits::kControllerFuid[0], Traits::kControllerFuid[1], Traits::kControllerFuid[2],
                        Traits::kControllerFuid[3]);
        Vst::IEditController* controller = nullptr;
        if (factory->createInstance (cid.toTUID(), Vst::IEditController::iid, reinterpret_cast<void**> (&controller))
                != kResultOk
            || controller == nullptr)
        {
            return false;
        }
        controller_ = owned (controller);
        if (controller_->initialize (static_cast<Vst::IComponentHandler*> (bridge_.get())) != kResultOk)
        {
            controller_ = nullptr;
            return false;
        }
        // The preset list shows the last selection: set it first (applying
        // that preset to the controller only, no handler yet), then the
        // plug-in's current values over it.
        if (Traits::numPresets() > 0)
            controller_->setParamNormalized (static_cast<Vst::ParamID> (Traits::kProgramParamId), plugin_.uiProgram_);
        for (int i = 0; i < kNumParams; ++i)
        {
            const ParamId id = idAt (i);
            lastPlain_[slot (id)] = plugin_.plainValue (id);
            controller_->setParamNormalized (tag (id), Traits::toNormalized (id, lastPlain_[slot (id)]));
        }
        lastBypass_ = plugin_.bypassed();
        controller_->setParamNormalized (static_cast<Vst::ParamID> (PluginT::kBypassId), lastBypass_ ? 1.0 : 0.0);
        controller_->setComponentHandler (bridge_);
        view_ = owned (controller_->createView (Vst::ViewType::kEditor));
        if (!view_ || view_->isPlatformTypeSupported (kPlatformTypeNSView) != kResultTrue)
            return false;
        view_->setFrame (bridge_);
        return true;
    }

    bool size (std::uint32_t& width, std::uint32_t& height) const
    {
        Steinberg::ViewRect rect {};
        if (view_->getSize (&rect) != Steinberg::kResultTrue)
            return false;
        width = static_cast<std::uint32_t> (rect.getWidth());
        height = static_cast<std::uint32_t> (rect.getHeight());
        return true;
    }

    bool attach (void* parent)
    {
        if (attached_ || parent == nullptr)
            return false;
        if (view_->attached (parent, Steinberg::kPlatformTypeNSView) != Steinberg::kResultTrue)
            return false;
        attached_ = true;
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer> ([this] (VSTGUI::CVSTGUITimer*) { pullPluginValues(); },
                                                          kTimerMs);
        return true;
    }

    /** The panel changed one value. */
    void edited (Steinberg::Vst::ParamID id, double normalized)
    {
        if (id < static_cast<Steinberg::Vst::ParamID> (kNumParams))
        {
            const auto paramId = static_cast<ParamId> (id);
            lastPlain_[slot (paramId)] = Traits::toPlain (paramId, normalized);
        }
        else if (id == static_cast<Steinberg::Vst::ParamID> (PluginT::kBypassId))
        {
            lastBypass_ = normalized >= 0.5;
        }
        plugin_.editFromUi (id, normalized, EditKind::value); // other ids (the preset list) are ignored
    }

    /** The controller changed values by itself (a preset): send the changed ones. */
    void pushControllerValues()
    {
        for (int i = 0; i < kNumParams; ++i)
        {
            const ParamId id = idAt (i);
            const double normalized = controller_->getParamNormalized (tag (id));
            const float plain = Traits::toPlain (id, normalized);
            if (plain == plugin_.plainValue (id))
                continue;
            lastPlain_[slot (id)] = plain;
            plugin_.editFromUi (tag (id), normalized, EditKind::value);
        }
    }

    /** UI timer: values the host or a state load changed reach the panel. */
    void pullPluginValues()
    {
        for (int i = 0; i < kNumParams; ++i)
        {
            const ParamId id = idAt (i);
            const float plain = plugin_.plainValue (id);
            if (plain == lastPlain_[slot (id)])
                continue;
            lastPlain_[slot (id)] = plain;
            controller_->setParamNormalized (tag (id), Traits::toNormalized (id, plain));
        }
        if (plugin_.bypassed() != lastBypass_)
        {
            lastBypass_ = plugin_.bypassed();
            controller_->setParamNormalized (static_cast<Steinberg::Vst::ParamID> (PluginT::kBypassId),
                                             lastBypass_ ? 1.0 : 0.0);
        }
        if constexpr (vst3::HasOutputMeterIds<Traits>)
        {
            controller_->setParamNormalized (static_cast<Steinberg::Vst::ParamID> (Traits::kMeterLevelParamIdL),
                                             static_cast<double> (plugin_.outputPeak (0)));
            controller_->setParamNormalized (static_cast<Steinberg::Vst::ParamID> (Traits::kMeterLevelParamIdR),
                                             static_cast<double> (plugin_.outputPeak (1)));
        }
        if constexpr (vst3::HasUiKeyboard<Traits>)
        {
            for (int w = 0; w < kHeldNoteWords; ++w)
            {
                const auto id = Traits::kHeldNotesParamIdBase + static_cast<std::uint32_t> (w);
                controller_->setParamNormalized (static_cast<Steinberg::Vst::ParamID> (id),
                                                 HeldNotes::wordToNormalized (plugin_.heldWord (w)));
            }
        }
    }

    static constexpr std::uint32_t kTimerMs = 33;

    PluginT& plugin_;
    Steinberg::IPtr<Bridge> bridge_;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller_;
    Steinberg::IPtr<Steinberg::IPlugView> view_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
    bool attached_ = false;
    std::array<float, static_cast<std::size_t> (kNumParams)> lastPlain_ {};
    bool lastBypass_ = false;
};

} // namespace kraftonic::formats::clap
