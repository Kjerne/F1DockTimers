; Inno Setup 6 script for the C++ build. Per-user install, no admin rights needed.
;
; Same AppId, folder, Start-menu entry and Run value as the Python build's installer, so this
; setup upgrades an installed Python copy in place (and the Python app's own updater can install
; it). Settings (%APPDATA%\F1DockTimers) and cache (%LOCALAPPDATA%\F1DockTimers) are shared with
; the Python build, so they carry over without an import step.
;
;   ISCC.exe /DAppVersion=1.1.0 packaging\installer.iss      (packaging\build.ps1 does this)
#ifndef AppVersion
  #define AppVersion "1.1.0"
#endif
; Tests build a copy under another id and name (/DTestBuild /DAppId=... /DName=F1DockTimersTest), so it has
; its own folder, shortcut, Run value and data folders and never touches a real install.
#ifndef AppId
  #define AppId "{{6E1B7C52-3A8F-4D9B-9F0E-F1A5C0DE2026}"
#endif
#ifndef Name
  #define Name "F1DockTimers"
#endif

[Setup]
AppId={#AppId}
AppName={#Name}
AppVersion={#AppVersion}
AppPublisher=Kjerne
AppPublisherURL=https://github.com/Kjerne/F1DockTimers
AppSupportURL=https://github.com/Kjerne/F1DockTimers/issues
AppUpdatesURL=https://github.com/Kjerne/F1DockTimers/releases
AppCopyright=Copyright (c) 2026 Kjerne
VersionInfoCompany=Kjerne
VersionInfoDescription=F1DockTimers Setup
VersionInfoVersion={#AppVersion}
DefaultDirName={localappdata}\Programs\{#Name}
DefaultGroupName={#Name}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\dist
OutputBaseFilename=F1DockTimers-{#AppVersion}-setup
SetupIconFile=..\assets\app.ico
UninstallDisplayIcon={app}\F1DockTimers.exe
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
CloseApplications=yes
LicenseFile=..\LICENSE

[Tasks]
Name: "autostart"; Description: "Start with Windows"; GroupDescription: "Options:"
Name: "desktopicon"; Description: "Desktop shortcut"; GroupDescription: "Options:"; Flags: unchecked
; Shown only when the folder still holds the Python build. Checked by default, also for the
; silent install the in-app updater runs.
Name: "cleanold"; Description: "Remove the files of the previous version (your settings are kept)"; \
    GroupDescription: "Upgrade:"; Check: HasPythonBuild

[InstallDelete]
; The Python build's runtime (PyInstaller puts everything but the exe in _internal).
Type: filesandordirs; Name: "{app}\_internal"; Tasks: cleanold
; Leftovers from running the C++ build beside the Python one during the port.
Type: files; Name: "{localappdata}\{#Name}\cache-cpp.json"
Type: files; Name: "{localappdata}\{#Name}\f1-cpp.log"

[Files]
Source: "..\dist\F1DockTimers\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{autoprograms}\{#Name}"; Filename: "{app}\F1DockTimers.exe"
Name: "{autodesktop}\{#Name}"; Filename: "{app}\F1DockTimers.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#Name}"; \
    ValueData: """{app}\F1DockTimers.exe"" --minimized"; Tasks: autostart; Flags: uninsdeletevalue
#ifndef TestBuild
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "NoctaliaF1"; Flags: deletevalue
#endif

; A test build (/DTestBuild) never starts, asks to quit or ends a running F1DockTimers: the
; real app may be running beside it.
#ifndef TestBuild
[Run]
Filename: "{app}\F1DockTimers.exe"; Parameters: "--show"; Description: "Launch F1DockTimers"; Flags: nowait postinstall skipifsilent
; In-app updates install silently: start the new version again afterwards.
Filename: "{app}\F1DockTimers.exe"; Parameters: "--show"; Flags: nowait skipifnotsilent

[UninstallRun]
Filename: "{app}\F1DockTimers.exe"; Parameters: "--quit"; Flags: runhidden; RunOnceId: "QuitApp"
Filename: "{cmd}"; Parameters: "/C taskkill /IM F1DockTimers.exe /F"; Flags: runhidden; RunOnceId: "KillApp"

#endif

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\{#Name}"

[Code]
function HasPythonBuild(): Boolean;
begin
  Result := DirExists(ExpandConstant('{app}\_internal'));
end;
