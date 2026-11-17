; Inno Setup 6 script. Per-user install, no admin rights needed.
#ifndef AppVersion
  #define AppVersion "1.0.2"
#endif

[Setup]
AppId={{6E1B7C52-3A8F-4D9B-9F0E-F1A5C0DE2026}
AppName=F1DockTimers
AppVersion={#AppVersion}
AppPublisher=Personal build
DefaultDirName={localappdata}\Programs\F1DockTimers
DefaultGroupName=F1DockTimers
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\dist
OutputBaseFilename=F1DockTimers-{#AppVersion}-setup
SetupIconFile=app.ico
UninstallDisplayIcon={app}\F1DockTimers.exe
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
CloseApplications=yes
LicenseFile=..\LICENSE

[Tasks]
Name: "autostart"; Description: "Start with Windows"; GroupDescription: "Options:"
Name: "desktopicon"; Description: "Desktop shortcut"; GroupDescription: "Options:"; Flags: unchecked

[Files]
Source: "..\dist\F1DockTimers\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{autoprograms}\F1DockTimers"; Filename: "{app}\F1DockTimers.exe"
Name: "{autodesktop}\F1DockTimers"; Filename: "{app}\F1DockTimers.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "F1DockTimers"; \
    ValueData: """{app}\F1DockTimers.exe"" --minimized"; Tasks: autostart; Flags: uninsdeletevalue

[Run]
Filename: "{app}\F1DockTimers.exe"; Parameters: "--show"; Description: "Launch F1DockTimers"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{cmd}"; Parameters: "/C taskkill /IM F1DockTimers.exe /F"; Flags: runhidden; RunOnceId: "KillApp"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "NoctaliaF1"; Flags: deletevalue

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\F1DockTimers"
