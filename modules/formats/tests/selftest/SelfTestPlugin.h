// SPDX-License-Identifier: MIT
#pragma once

// Test-only VST3 instrument proving the shared base.
// Never installed or shipped; validates the shared VST3 adapter.

#include "SelfTestTraits.h"
#include "kraftonic/formats/vst3/Vst3Controller.h"
#include "kraftonic/formats/vst3/Vst3Processor.h"

namespace kraftonic::formats::selftest
{

using SelfTestProcessor = vst3::Processor<SelfTestTraits>;
using SelfTestController = vst3::Controller<SelfTestTraits>;

} // namespace kraftonic::formats::selftest
