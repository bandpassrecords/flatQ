; Inno Setup script. Build: iscc /DAppVersion=0.0.1 installers\windows\flatq.iss  (output: dist\FlatQ-<version>-Windows-Setup.exe)
; Expects the Release build in build\FlatQ_artefacts\Release.
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{A78FA30F-71A6-4866-9B23-F39F8D3C7FB9}
AppName=FlatQ
AppVersion={#AppVersion}
AppPublisher=Bandpass Records
DefaultDirName={autopf}\FlatQ
DefaultGroupName=FlatQ
OutputDir=..\..\dist
OutputBaseFilename=FlatQ-{#AppVersion}-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=FlatQ

[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (C:\Program Files\Common Files\VST3)"; Types: full custom
Name: "standalone"; Description: "Standalone app"; Types: full custom

[Files]
; VST3 on Windows is a folder bundle
Source: "..\..\build\FlatQ_artefacts\Release\VST3\FlatQ.vst3\*"; DestDir: "{commoncf64}\VST3\FlatQ.vst3"; Components: vst3; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "..\..\build\FlatQ_artefacts\Release\Standalone\FlatQ.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{group}\FlatQ"; Filename: "{app}\FlatQ.exe"; Components: standalone
Name: "{group}\Uninstall FlatQ"; Filename: "{uninstallexe}"
