# Building Kraftonic Studio

| | Windows | macOS | Linux |
|---|---|---|---|
| Compiler | Visual Studio 2022+ (MSVC) | Xcode 15+ | GCC 11+ / Clang 15+ |
| CMake | 3.25+ | 3.25+ | 3.25+ |
| Generator | Visual Studio (default) | Ninja or Xcode | Ninja |
| Target OS | Windows 10+ x64 | macOS 11+ (arm64 + x86_64) | Ubuntu 22.04+ x64 (X11) |

On Linux, install the packages needed by VSTGUI and the SDK's validator:

```sh
sudo apt-get install build-essential cmake ninja-build $(cat scripts/linux-dependencies.txt)
```

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release        # add -G Ninja on macOS/Linux
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

- The first configure downloads the pinned VST3 SDK, the CLAP headers and Catch2, and on macOS Apple's AudioUnitSDK. To reuse a local SDK checkout, pass `-DFETCHCONTENT_SOURCE_DIR_VST3SDK=/path/to/vst3sdk` (`-DFETCHCONTENT_SOURCE_DIR_CLAP=...` for CLAP, `-DFETCHCONTENT_SOURCE_DIR_AUDIOUNITSDK=...` for the AudioUnitSDK).
- For a DSP-only build with no SDK, pass `-DKRAFTONIC_BUILD_PLUGINS=OFF`.
- On macOS, `KRAFTONIC_BUILD_AU` (ON by default; ignored elsewhere) also builds each plug-in as an Audio Unit (AUv2) for Logic Pro and GarageBand. Pass `-DKRAFTONIC_BUILD_AU=OFF` to build the VST3 only.
- `KRAFTONIC_BUILD_CLAP` (ON by default with the plug-ins) also builds each plug-in as a CLAP plug-in. Pass `-DKRAFTONIC_BUILD_CLAP=OFF` to skip it.
- For a universal macOS build, pass `"-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"`.

The plug-ins are written to `build/VST3/Release/`, for example `KraftonicStarterSynth.vst3` (hosts display it as "Kraftonic Starter Synth"; the bundle file name has no space because of a Windows/MSBuild limitation in the SDK's resource copy). On macOS the Audio Unit `KraftonicStarterSynth.component` lands next to it (target `KraftonicStarterSynth_AU`). It is the VST SDK's AU wrapper with a copy of the `.vst3` inside, so it sounds and looks the same.

The CLAP plug-in `KraftonicStarterSynth.clap` (target `KraftonicStarterSynth_CLAP`) lands next to it on every system. It is the same engine, parameters, presets and state compiled against `modules/formats/include/kraftonic/formats/clap/` (a bundle on macOS, one file elsewhere), and it loads a VST3 project's state. Its CLAP id is the plug-in's `BUNDLE_IDENTIFIER`, permanent once shipped. The panel opens on macOS; on Windows and Linux the CLAP plug-in has no editor yet (see [modules/formats/README.md](../modules/formats/README.md)).

Each plug-in's Audio Unit identity is set in its `kraftonic_add_plugin` call: `AU_SUBTYPE` and `AU_MANUFACTURER`, four letters or digits each, with at least one upper-case letter in the manufacturer code. The type follows the plug-in's kind (`aumu` instrument, `aufx` effect). Like the VST3 class IDs, these codes are permanent once shipped.

## Validate

```sh
cmake --build build --config Release --target validator
build/bin/Release/validator "build/VST3/Release/KraftonicStarterSynth.vst3"
# Audio Units (macOS): auval, with each component briefly installed in ~/Library
scripts/ci/auval.sh build/logs build/VST3/Release/*.component
# CLAP: clap-validator (pinned release, downloaded once to ~/.cache/kraftonic/tools)
scripts/ci/clap-validator.sh build/logs build/VST3/Release/*.clap
# pluginval (https://github.com/Tracktion/pluginval/releases/tag/v1.0.4):
pluginval --strictness-level 10 --validate-in-process --vst3validator build/bin/Release/validator \
  "build/VST3/Release/KraftonicStarterSynth.vst3"
```

## Continuous integration

GitHub Actions builds every pull request on macOS (universal), Windows and Linux: unit tests, every plug-in, the Steinberg validator and pluginval on each bundle, clap-validator on each CLAP plug-in, auval on each Audio Unit (macOS), and the installers (`.github/workflows/build.yml`). Locally, `scripts/check.sh <product>` runs the same checks natively.

## Advanced: Studio as a dependency of another repository

Forking Studio and adding your plug-in under `plugins/` is the simple way. To keep a plug-in in a repository of its own instead, copy its `plugins/<id>` folder there and give the repository a top-level `CMakeLists.txt` like this:

```cmake
cmake_minimum_required(VERSION 3.25)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version") # before project()
project(MySynth LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJC OBJCXX)
endif()
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
FetchContent_Declare(kraftonic_studio
    GIT_REPOSITORY https://github.com/kraftonic/studio.git
    GIT_TAG        <full commit hash>)
FetchContent_MakeAvailable(kraftonic_studio)
kraftonic_enable_studio()            # the SDK's compiler settings and Catch2, for this folder

add_subdirectory(plugins/mysynth)    # the same layout as plugins/<id> in Studio
```

The plug-in folder's `CMakeLists.txt` builds its engine and makes one `kraftonic_add_plugin(...)` call, as in the starters; the helpers are documented in `cmake/KraftonicPlugin.cmake`.

| Option | Top level | Subproject | Effect |
|---|---|---|---|
| `KRAFTONIC_BUILD_STARTERS` | ON | OFF | the starters in `plugins/` |
| `KRAFTONIC_BUILD_MODULE_TESTS` | ON | OFF | Studio's own module tests |
| `KRAFTONIC_BUILD_TESTS` | ON | ON | Catch2 and the plug-ins' tests |
| `KRAFTONIC_BUILD_PLUGINS` | ON | ON | the VST3 SDK and the bundles |
| `KRAFTONIC_BUILD_AU` | ON | ON | macOS only: the AudioUnitSDK and an Audio Unit of each plug-in |
| `KRAFTONIC_BUILD_CLAP` | ON | ON | the CLAP headers and a CLAP plug-in of each plug-in |

- Develop against a local Studio checkout: `-DFETCHCONTENT_SOURCE_DIR_KRAFTONIC_STUDIO=/path/to/studio`.
- Every consumer build downloads the VST3 SDK and Catch2 once per build tree; reuse a checkout with `-DFETCHCONTENT_SOURCE_DIR_VST3SDK=...`.
- Pin `GIT_TAG` to a full commit hash and upgrade on purpose; `scripts/check.sh` in the repository checks the result.

## Install for testing

Copy the `.vst3` bundle into your plug-in folder:

| OS | Folder |
|---|---|
| Windows | `C:\Program Files\Common Files\VST3\` |
| macOS | `~/Library/Audio/Plug-Ins/VST3/` |
| Linux | `~/.vst3/` |

On macOS, copy the `.component` (the Audio Unit) into `~/Library/Audio/Plug-Ins/Components/`. Logic Pro rescans Audio Units when it starts, so quit and reopen it; GarageBand does the same. If a plug-in doesn't show up, run `auval -a` in Terminal to see whether macOS has registered it.

Copy the `.clap` into the CLAP folder: `C:\Program Files\Common Files\CLAP\` (Windows), `~/Library/Audio/Plug-Ins/CLAP/` (macOS), `~/.clap/` (Linux). The installers in `packaging/` put it there for you.
