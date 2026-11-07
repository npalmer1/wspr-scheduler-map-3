; WSPR Scheduler Map 3 - Windows installer (Inno Setup 6, https://jrsoftware.org/isinfo.php)
;
; Don't compile this by hand: run make_release.bat, which fills the "stage" folder with the program,
; the Qt files and the MySQL driver, then compiles this script.
; The version comes from WsprMap.exe (project(VERSION) in CMakeLists.txt).

#define AppName   "WSPR Scheduler Map 3"
#define AppExe    "WsprMap.exe"
#define AppUrl    "https://github.com/npalmer1/wspr-scheduler-map-3"
#define StageDir  "stage"

#ifndef AppVersion
  #define AppVersion GetStringFileInfo(AddBackslash(SourcePath) + StageDir + "\" + AppExe, "ProductVersion")
#endif
#if AppVersion == ""
  #error stage\WsprMap.exe is missing or has no version - run make_release.bat
#endif

[Setup]
; never change AppId: it is how Windows knows a new version replaces the old one
AppId={{668951A4-B64C-4292-9D30-0180EA721EEE}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=G4GCI
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} setup
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
InfoBeforeFile=requirements.txt
SetupIconFile=..\wsprmap.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
OutputDir=output
OutputBaseFilename=WSPR-Scheduler-Map-3-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "requirements.txt"; DestDir: "{app}"; Flags: ignoreversion

[INI]
Filename: "{app}\WSPR Scheduler Map 3 on GitHub.url"; Section: "InternetShortcut"; Key: "URL"; String: "{#AppUrl}"

[Icons]
Name: "{autoprograms}\{#AppName}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autoprograms}\{#AppName}\Requirements"; Filename: "{app}\requirements.txt"
Name: "{autoprograms}\{#AppName}\WSPR Scheduler Map 3 on GitHub"; Filename: "{app}\WSPR Scheduler Map 3 on GitHub.url"
Name: "{autoprograms}\{#AppName}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: files; Name: "{app}\WSPR Scheduler Map 3 on GitHub.url"
