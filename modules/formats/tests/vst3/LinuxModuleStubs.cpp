// SPDX-License-Identifier: MIT
//
// The test executable links the shared VST3 base, whose VSTGUI editor glue
// refers to the plug-in module entry points. A real plug-in gets them from its
// factory and the SDK's linuxmain.cpp; this test is not a plug-in, so on Linux
// (where GNU ld resolves every pulled-in object) it provides inert stand-ins.
#include "pluginterfaces/base/ipluginbase.h"

#if SMTG_OS_LINUX
void* moduleHandle = nullptr;
SMTG_EXPORT_SYMBOL Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory()
{
    return nullptr;
}
#endif
