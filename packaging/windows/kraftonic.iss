; SPDX-License-Identifier: MIT
;
; Inno Setup 6 script for the Kraftonic Studio VST3 installer (x64).
; Don't compile this file directly: packaging/windows/build-installer.sh stages
; the bundles, generates products.iss (one component per built plug-in) and
; runs ISCC with /DAppVersion=... (and /DSignTool when a certificate is set).

#ifndef AppVersion
  #error AppVersion must be defined (ISCC /DAppVersion=x.y.z)
#endif
#ifndef AppVersionNumeric
  #define AppVersionNumeric AppVersion
#endif

[Setup]
; Keep AppId fixed forever: it is how upgrades and the uninstaller find earlier installs.
AppId={{D76B0F0B-CFC3-4E16-8978-2B9A086481E2}
AppName=Kraftonic Studio
AppVersion={#AppVersion}
AppVerName=Kraftonic Studio {#AppVersion}
AppPublisher=Kraftonic
AppPublisherURL=https://github.com/kraftonic/studio
VersionInfoVersion={#AppVersionNumeric}
VersionInfoProductVersion={#AppVersionNumeric}
VersionInfoDescription=Kraftonic Studio VST3 plug-ins installer
; VST3 plug-ins always go to the system-wide Common Files folder.
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
UsePreviousAppDir=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile=LICENSE.txt
InfoBeforeFile=welcome.txt
OutputDir=output
OutputBaseFilename=KraftonicStudio-{#AppVersion}-Windows-x64-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
Uninstallable=yes
UninstallDisplayName=Kraftonic Studio {#AppVersion}
CloseApplications=no
#ifdef SignTool
SignTool=kraftonic
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "All plug-ins"
Name: "custom"; Description: "Choose plug-ins"; Flags: iscustom

; [Components], [Files] and [UninstallDelete] for every built plug-in.
#include "products.iss"
