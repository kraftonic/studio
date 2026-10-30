// SPDX-License-Identifier: MIT
//
// Generic editor snapshot tool. Loads any Kraftonic .vst3 bundle like a host does, opens its
// editor in an off-screen window and draws the editor view into a bitmap, so
// screenshots need no host and no screen-recording permission:
//      kraftonic_editor_snapshot <bundle.vst3> <out.png> [scale]
// `scale` is the pixel density (default 2 = Retina).

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace Steinberg;

namespace
{

int fail (const char* message, const std::string& detail = {})
{
    std::fprintf (stderr, "kraftonic_editor_snapshot: %s%s%s\n", message, detail.empty() ? "" : ": ",
                  detail.c_str());
    return 2;
}

// Asks every layer below `layer` that has a drawing delegate to draw itself
// into `ctx` (layer coordinates, y up). VSTGUI draws its frame this way.
void drawLayerTree (CALayer* layer, CGContextRef ctx, double scale)
{
    CGContextSaveGState (ctx);
    CGContextTranslateCTM (ctx, layer.frame.origin.x, layer.frame.origin.y);
    CGContextClipToRect (ctx, layer.bounds);
    id delegate = layer.delegate;
    if (delegate != nil && [delegate respondsToSelector:@selector (drawLayer:inContext:)])
    {
        layer.contentsScale = scale;
        CGContextSaveGState (ctx);
        if ([layer contentsAreFlipped])
        {
            // The delegate draws top-down; the bitmap context is bottom-up.
            CGContextTranslateCTM (ctx, 0, layer.bounds.size.height);
            CGContextScaleCTM (ctx, 1, -1);
        }
        [delegate drawLayer:layer inContext:ctx];
        CGContextRestoreGState (ctx);
    }
    for (CALayer* sub in layer.sublayers)
        drawLayerTree (sub, ctx, scale);
    CGContextRestoreGState (ctx);
}

// Draws `view` into a bitmap of `scale` pixels per point.
NSBitmapImageRep* capture (NSView* view, double scale)
{
    const NSRect bounds = [view bounds];
    const auto pw = static_cast<size_t> (bounds.size.width * scale);
    const auto ph = static_cast<size_t> (bounds.size.height * scale);
    if (view.layer != nil)
    {
        CGColorSpaceRef space = CGColorSpaceCreateWithName (kCGColorSpaceSRGB);
        CGContextRef ctx = CGBitmapContextCreate (nullptr, pw, ph, 8, 0, space,
                                                  kCGImageAlphaPremultipliedLast);
        CGColorSpaceRelease (space);
        if (ctx == nullptr)
            return nil;
        CGContextScaleCTM (ctx, scale, scale);
        CALayer* root = view.layer;
        CGContextSaveGState (ctx);
        // The view's own layer sits at the origin; only its sublayers move.
        CGContextClipToRect (ctx, root.bounds);
        for (CALayer* sub in root.sublayers)
            drawLayerTree (sub, ctx, scale);
        CGContextRestoreGState (ctx);
        CGImageRef image = CGBitmapContextCreateImage (ctx);
        CGContextRelease (ctx);
        if (image == nullptr)
            return nil;
        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithCGImage:image];
        CGImageRelease (image);
        return rep;
    }
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:nullptr
                      pixelsWide:static_cast<NSInteger> (pw)
                      pixelsHigh:static_cast<NSInteger> (ph)
                   bitsPerSample:8
                 samplesPerPixel:4
                        hasAlpha:YES
                        isPlanar:NO
                  colorSpaceName:NSCalibratedRGBColorSpace
                     bytesPerRow:0
                    bitsPerPixel:0];
    if (rep == nil)
        return nil;
    [rep setSize:bounds.size]; // points; the pixel size above sets the density
    [view cacheDisplayInRect:bounds toBitmapImageRep:rep];
    return rep;
}

// True when every pixel has the same colour (nothing was drawn).
bool isBlank (NSBitmapImageRep* rep)
{
    const unsigned char* data = [rep bitmapData];
    const NSInteger rowBytes = [rep bytesPerRow];
    const NSInteger w = [rep pixelsWide], h = [rep pixelsHigh];
    for (NSInteger y = 0; y < h; ++y)
        for (NSInteger x = 0; x < w; ++x)
            for (int c = 0; c < 4; ++c)
                if (data[y * rowBytes + x * 4 + c] != data[c])
                    return false;
    return true;
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 3 || argc > 4)
    {
        std::fprintf (stderr, "usage: %s <bundle.vst3> <out.png> [scale]\n", argv[0]);
        return 1;
    }
    const double scale = argc == 4 ? std::atof (argv[3]) : 2.0;
    if (!(scale >= 1.0 && scale <= 4.0))
        return fail ("scale must be between 1 and 4");

    @autoreleasepool
    {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];

        // Host side: application context, module, component + controller
        // (PlugProvider connects them and copies the component state across).
        Vst::HostApplication hostApp;
        Vst::PluginContextFactory::instance().setPluginContext (&hostApp);

        std::string error;
        auto module = VST3::Hosting::Module::create (argv[1], error);
        if (!module)
            return fail ("cannot load bundle", error);

        IPtr<Vst::PlugProvider> provider;
        for (auto& info : module->getFactory().classInfos())
        {
            if (info.category() == kVstAudioEffectClass)
            {
                provider = owned (new Vst::PlugProvider (module->getFactory(), info, true));
                break;
            }
        }
        if (!provider || provider->initialize() == false)
            return fail ("no audio processor class in", argv[1]);
        IPtr<Vst::IEditController> controller = provider->getControllerPtr();
        if (!controller)
            return fail ("no edit controller");

        IPtr<IPlugView> view = owned (controller->createView (Vst::ViewType::kEditor));
        if (!view)
            return fail ("controller has no editor");
        if (view->isPlatformTypeSupported (kPlatformTypeNSView) != kResultTrue)
            return fail ("editor does not support NSView");
        ViewRect rect {};
        if (view->getSize (&rect) != kResultOk || rect.getWidth() <= 0 || rect.getHeight() <= 0)
            return fail ("editor reports no size");

        const NSRect frame = NSMakeRect (0, 0, rect.getWidth(), rect.getHeight());
        NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                       styleMask:NSWindowStyleMaskBorderless
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        [window setReleasedWhenClosed:NO];
        NSView* parent = [window contentView];
        if (view->attached ((__bridge void*)parent, kPlatformTypeNSView) != kResultOk)
            return fail ("editor failed to attach");
        // Let the editor finish building and pick up parameter values.
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.3]];

        // A host sizes the editor after attaching it; off screen nothing else
        // would (VSTGUI sets its drawing layer's frame from this call).
        view->onSize (&rect);
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
        NSView* editorView = [[parent subviews] firstObject];
        if (editorView == nil)
            editorView = parent;

        NSBitmapImageRep* rep = capture (editorView, scale);
        NSData* png = rep ? [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] : nil;
        const bool drawn = rep != nil && !isBlank (rep);

        view->removed();
        view = nullptr;
        [window close];
        controller = nullptr;
        provider = nullptr;
        module = nullptr;
        Vst::PluginContextFactory::instance().setPluginContext (nullptr);

        if (!drawn)
            return fail ("editor drew nothing");
        NSString* out = [NSString stringWithUTF8String:argv[2]];
        if (png == nil || ![png writeToFile:out atomically:YES])
        {
            std::fprintf (stderr, "kraftonic_editor_snapshot: cannot write %s\n", argv[2]);
            return 3;
        }
        std::printf ("wrote %s (%d x %d at %gx)\n", argv[2], rect.getWidth(), rect.getHeight(), scale);
    }
    return 0;
}
