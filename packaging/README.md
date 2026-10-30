# Packaging

Scripts that turn built `*.vst3` bundles into an installer for each OS. CI builds them **unsigned** on every pull request, to keep the scripts working. Use them for your own plug-ins, signed with your own certificates (see the end of this page).

Every script discovers products from the built `*.vst3` bundles and packages each available bundle.

| OS | Output | Installs to | Per-product choice |
|---|---|---|---|
| macOS 11+ (universal) | `KraftonicStudio-<ver>-macOS.pkg` | `/Library/Audio/Plug-Ins/VST3`, and the Audio Units in `/Library/Audio/Plug-Ins/Components` | "Customize" on the Installation Type step |
| Windows 10+ x64 | `KraftonicStudio-<ver>-Windows-x64-Setup.exe` | `C:\Program Files\Common Files\VST3` | "Choose plug-ins" type; uninstaller in Settings > Apps |
| Linux | `KraftonicStudio-<ver>-Linux-<arch>.tar.gz` | `~/.vst3`, or `/usr/lib/vst3` with `--system` | copy only the bundles you want, or delete them after |

The all-in-one installers carry the Studio version (`project(KraftonicStudio VERSION x.y.z)` in the root `CMakeLists.txt`, read by `cmake -P cmake/KraftonicPrintVersion.cmake`). Each **bundle** carries its own product version from `plugins/<id>/VERSION` (one line, `x.y.z`; read with `cmake -DPRODUCT=<id> -P cmake/KraftonicPrintVersion.cmake`). `build-pkg.sh` refuses a bundle whose Info.plist or moduleinfo.json version differs from its product's `VERSION`.

## Building installers locally

```sh
# macOS (after the full build in docs/SPEC.md section 4)
packaging/macos/build-pkg.sh build/mac/VST3/Release build/installers
pkgutil --payload-files build/installers/KraftonicStudio-*-macOS.pkg   # inspect
pkgutil --expand-full build/installers/KraftonicStudio-*-macOS.pkg /tmp/pkg-x   # full payload

# Linux (in CI, or on a Linux machine)
packaging/linux/build-tarball.sh build/VST3/Release build/installers

# Windows (Git Bash, Inno Setup 6 installed)
packaging/windows/build-installer.sh build/VST3/Release build/installers
```

All three take `<vst3-dir> <out-dir> [version]`.

Notes:
- **macOS:** each product is its own component package (`com.kraftonic.studio.<id>.vst3`) holding its `.vst3` and, when one was built next to it, its `.component`, not relocatable and not version-checked, so running an installer always installs exactly its version into the system folder. On some dev machines, macOS adds a `com.apple.provenance` attribute that can't be removed. It then shows up as `._*` entries in `pkgutil --payload-files`. These are harmless: Installer restores them as attributes, not files. CI runners don't add it.
- **Windows:** `kraftonic.iss` is the fixed part. `build-installer.sh` generates `products.iss` (`[Components]`, `[Files]`, `[UninstallDelete]`) from the bundles. Keep the `AppId` GUID unchanged forever, because upgrades and the uninstaller depend on it. GitHub's `windows-latest` (Windows Server 2025) image ships Inno Setup 6.7.1. CI falls back to `choco install innosetup` if an image drops it.
- **Linux:** `install.sh --uninstall [--system]` removes the bundles again.

## CI (`.github/workflows/build.yml`)

Runs on every pull request into `main`, or by hand:
- `lint` runs actionlint and shellcheck on the workflows and scripts.
- On each OS, the `build` job runs:
  1. Configure and build with warnings as errors.
  2. `ctest`. Test names are prefixed by module or product, and the summary counts them per prefix.
  3. `scripts/ci/validate-plugins.sh` on **every** bundle: the Steinberg validator, then pluginval at strictness 10. Linux uses `xvfb-run`, `--skip-gui-tests` and no `--vst3validator`; see `docs/KNOWN_ISSUES.md`. On macOS, `scripts/ci/auval.sh` then runs auval on every Audio Unit.
  4. Builds the installer.
  5. Uploads `KraftonicStudio-VST3-<OS>`, `installer-<OS>` and `logs-<OS>`.
  6. Writes a per-product summary (`scripts/ci/summarize.sh`) to the job summary.

## Per-product packages

`packaging/macos/build-pkg.sh --product <id> <vst3-dir> <out-dir>` builds a `.pkg` for one product only.

## Signing your own installers

The scripts sign when you set these environment variables, and skip signing otherwise:

- **macOS:** `KRAFTONIC_MAC_APP_IDENTITY` ("Developer ID Application: ...") signs each bundle with `codesign --options runtime --timestamp`; `KRAFTONIC_MAC_INSTALLER_IDENTITY` ("Developer ID Installer: ...") signs the `.pkg`; `KRAFTONIC_MAC_KEYCHAIN` names the keychain that holds them. Notarize the signed `.pkg` yourself afterwards (`xcrun notarytool submit --wait`, then `xcrun stapler staple`).
- **Windows:** `KRAFTONIC_WIN_CERT_PFX` and `KRAFTONIC_WIN_CERT_PASSWORD` make `signtool` sign each plug-in binary, and Inno Setup sign the setup and uninstaller (SHA-256, RFC 3161 timestamp from `KRAFTONIC_WIN_TIMESTAMP_URL`, default `http://timestamp.digicert.com`).

Studio's own CI never signs and holds no signing secrets.
