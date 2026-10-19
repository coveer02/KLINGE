; Inno Setup 6 script for KLINGE (VST3 + CLAP, 64 bit)
; Build from the repository root after building Release:
;   ISCC.exe /DAppVersion=2.0.0 installer\windows\KLINGE.iss

#ifndef AppVersion
  #define AppVersion "2.0.0"
#endif
#define Artefacts "..\..\build\KLINGE_artefacts\Release"

[Setup]
AppId={{69D964DE-1B75-4887-8623-391FB21DC774}
AppName=KLINGE
AppVersion={#AppVersion}
AppVerName=KLINGE {#AppVersion}
AppPublisher=KLINGE Audio
DefaultDirName={commonpf64}\KLINGE
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir=Output
OutputBaseFilename=KLINGE-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=KLINGE {#AppVersion}

[Types]
Name: "full"; Description: "VST3 and CLAP"
Name: "custom"; Description: "Choose formats"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (Ableton Live, Cubase, FL Studio, Studio One, ...)"; Types: full custom
Name: "clap"; Description: "CLAP plugin (Bitwig, Reaper, FL Studio, ...)"; Types: full custom

[Files]
Source: "{#Artefacts}\VST3\KLINGE.vst3\*"; DestDir: "{commoncf64}\VST3\KLINGE.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Artefacts}\CLAP\KLINGE.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
Source: "..\..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\KLINGE.vst3"
