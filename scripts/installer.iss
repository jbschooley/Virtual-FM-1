; Inno Setup script for the Windows installer.
;   ISCC.exe /DVersion=0.1.0 /DBuildDir=..\build /DOutDir=..\dist scripts\installer.iss
#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build"
#endif
#ifndef OutDir
  #define OutDir "..\dist"
#endif
#define Name "Virtual FM-1"
#define Art BuildDir + "\VirtualFM1_artefacts\Release"

[Setup]
AppId={{9E2C1D0A-6C2B-4D3E-9B7B-5A1F0C0D2E31}
AppName={#Name}
AppVersion={#Version}
AppPublisher=Bockage
AppPublisherURL=https://github.com/jbschooley/Virtual-FM-1
DefaultDirName={autopf}\{#Name}
DefaultGroupName={#Name}
OutputDir={#OutDir}
OutputBaseFilename=Virtual-FM-1-{#Version}-Windows
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\LICENSE
PrivilegesRequired=admin

[Types]
Name: "full"; Description: "VST3 plugin and standalone app"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (for Gig Performer, Reaper, Cubase, ...)"; Types: full custom; Flags: fixed
Name: "standalone"; Description: "Standalone application"; Types: full custom

[Files]
Source: "{#Art}\VST3\{#Name}.vst3\*"; DestDir: "{commoncf64}\VST3\{#Name}.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Art}\Standalone\{#Name}.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{group}\{#Name}"; Filename: "{app}\{#Name}.exe"; Components: standalone
Name: "{group}\Uninstall {#Name}"; Filename: "{uninstallexe}"
