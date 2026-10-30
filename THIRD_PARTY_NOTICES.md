# Third-party notices

Kraftonic Studio's own source code is MIT-licensed (see `LICENSE`). Plug-ins built with the kit include the following third-party software, whose licences must be reproduced with binary distributions.

| Component | Version | Licence | Source |
|---|---|---|---|
| Steinberg VST3 SDK (base, pluginterfaces, public.sdk) | 3.8.1 (`3cdf9ca`) | MIT | https://github.com/steinbergmedia/vst3sdk |
| VSTGUI | as pinned by VST3 SDK 3.8.1 | BSD-3-Clause | https://github.com/steinbergmedia/vstgui |
| Apple AudioUnitSDK (macOS Audio Units only) | 1.4.0 (`bd98b31`) | Apache-2.0 | https://github.com/apple/AudioUnitSDK |
| CLAP (headers, CLAP plug-ins only) | 1.2.10 (`195b42a`) | MIT | https://github.com/free-audio/clap |

The panel typefaces ship inside every plug-in bundle under `Resources/Fonts/`: **Barlow Condensed** (Light, SemiBold, Bold) for silkscreen labels and titles, and **Barlow Semi Condensed** (Regular, Medium, SemiBold) for values and OLED readouts, both © 2017 The Barlow Project Authors (https://github.com/jpt/barlow), SIL Open Font License 1.1, with `OFL-Barlow.txt`. The vendored copies live in `modules/ui/kit/assets/fonts/`.

Build and test tools that are **not** distributed: Catch2 (BSL-1.0), pluginval (GPL-3.0), clap-validator (MIT), and CMake/Ninja.

The macOS Audio Units contain the VST3 SDK's AU wrapper (`public.sdk/source/vst/auwrapper`, MIT, part of the SDK above) built on the AudioUnitSDK.

The CLAP plug-ins also contain the VST3 SDK and VSTGUI above (the panel is the VST3 editor).

The full licence texts are in the fetched sources: `vst3sdk/LICENSE.txt`, `vst3sdk/vstgui4/LICENSE`, `AudioUnitSDK/LICENSE.txt` and `clap/LICENSE`. The macOS installer shows them, with this file, on its licence step.

"VST" is a trademark of Steinberg Media Technologies GmbH. Kraftonic is not affiliated with Steinberg.
