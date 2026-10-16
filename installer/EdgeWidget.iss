; Installer for EdgeWidget (Inno Setup 6). Built by scripts\package.ps1, which passes
; AppVersion (from CMakeLists.txt) and StageDir (a fresh `cmake --install` of the app).
;
; Per-user install, no admin rights: %LOCALAPPDATA%\Programs\EdgeWidget.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\out\stage"
#endif
#define AppName "Edge Widget"
#define AppExe "EdgeWidget.exe"
; Same value name as app/Autostart.cpp, so the setting in the app and this task agree.
#define RunKey "Software\Microsoft\Windows\CurrentVersion\Run"
#define RunValue "EdgeWidget"

[Setup]
; Never change AppId: upgrades and the uninstaller find the installed app by it.
AppId={{AD2D5A88-1B79-4A3E-8428-0885C2DC4ECC}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
DefaultDirName={autopf}\EdgeWidget
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 1809+: the media session API the widget uses.
MinVersion=10.0.17763
OutputDir=..\out
OutputBaseFilename=EdgeWidget-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Same vinyl as the exe (resources\EdgeWidget.rc).
SetupIconFile=..\resources\EdgeWidget.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
; Close a running widget before replacing its files.
CloseApplications=force
RestartApplications=no

[Languages]
Name: "uk"; MessagesFile: "compiler:Languages\Ukrainian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
uk.Autostart=Запускати разом з Windows
en.Autostart=Start with Windows

[Tasks]
Name: "autostart"; Description: "{cm:Autostart}"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; Leftovers of older builds that are no longer deployed.
Type: files; Name: "{app}\opengl32sw.dll"
Type: files; Name: "{app}\D3Dcompiler_47.dll"

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "{#RunKey}"; ValueType: string; ValueName: "{#RunValue}"; ValueData: """{app}\{#AppExe}"""; Tasks: autostart

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/IM {#AppExe} /F"; Flags: runhidden; RunOnceId: "CloseWidget"

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
begin
  // Unticked on (re)install: also drop an autostart entry left by the app's own setting.
  if (CurStep = ssPostInstall) and not WizardIsTaskSelected('autostart') then
    RegDeleteValue(HKEY_CURRENT_USER, '{#RunKey}', '{#RunValue}');
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  // The app can turn autostart on by itself, so remove it whoever created it.
  // Settings (HKCU\Software\EdgeWidget) and the Spotify login stay for a reinstall.
  if CurUninstallStep = usPostUninstall then
    RegDeleteValue(HKEY_CURRENT_USER, '{#RunKey}', '{#RunValue}');
end;
