; Inno Setup script for HumHouse Drums X
; Produces HumHouse-Drums-X-Windows-Setup.exe that installs the VST3 into
; C:\Program Files\Common Files\VST3\ and the Standalone into Program Files.
;
; Run from the repository root on Windows after a Release build:
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\windows\HumHouseDrums.iss

#define MyAppName "HumHouse Drums X"
#define MyAppVersion "1.9.8"
#define MyAppPublisher "HumHouse"
#define MyAppURL "https://github.com/elijahjfrierson-prog/humhouse-drum-vst"
#define MyAppExeName "HumHouse Drums X.exe"

[Setup]
AppId={{7A3E9C12-4F1B-4D21-9B0C-HUMHOUSEDRUMS}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\HumHouse\HumHouse Drums X
DefaultGroupName=HumHouse
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE.txt
OutputDir=..\..\
OutputBaseFilename=HumHouse-Drums-X-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
PrivilegesRequired=admin
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName} {#MyAppVersion}
; v1.6.1-rc.10 — HumHouse crest in the installer wizard. Sidebar image
; shows on the welcome / finish pages, small image sits top-right of the
; remaining wizard pages, .ico drives the installer + uninstaller icon.
WizardImageFile=..\..\Resources\Icons\InstallerSidebar.bmp
WizardSmallImageFile=..\..\Resources\Icons\InstallerSmall.bmp
SetupIconFile=..\..\Resources\Icons\HumHouseAppIcon.ico

; An install always replaces whatever was there before: the AppId above makes
; Inno upgrade the previous entry in place, and these deletions clear content
; and plug-in copies an older setup left behind, so no stale kit or corpus
; file survives into the new version.
[InstallDelete]
Type: filesandordirs; Name: "{commonappdata}\HumHouse\Drums X\Content"
Type: filesandordirs; Name: "{commoncf64}\VST3\HumHouse Drums X.vst3"
Type: filesandordirs; Name: "{commoncf32}\VST3\HumHouse Drums X.vst3"
Type: filesandordirs; Name: "{localappdata}\Programs\Common\VST3\HumHouse Drums X.vst3"
Type: files; Name: "{app}\{#MyAppExeName}"

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut for HumHouse Drums X (Standalone)"; GroupDescription: "Additional shortcuts:"

[Files]
; Standalone .exe -> Program Files\HumHouse\HumHouse Drums X\
Source: "..\..\build\AIDrumVST_artefacts\Release\Standalone\HumHouse Drums X.exe"; \
  DestDir: "{app}"; Flags: ignoreversion

; VST3 bundle -> C:\Program Files\Common Files\VST3\HumHouse Drums X.vst3\
; VST3 is a *folder*; recurse the whole bundle.
Source: "..\..\build\AIDrumVST_artefacts\Release\VST3\HumHouse Drums X.vst3\*"; \
  DestDir: "{commoncf64}\VST3\HumHouse Drums X.vst3"; \
  Flags: ignoreversion recursesubdirs createallsubdirs

; Groove corpus + kit content -> shared location, version-checked at load by
; the plug-in (see DrumsXProcessor::loadContent).
Source: "..\..\content\*"; \
  DestDir: "{commonappdata}\HumHouse\Drums X\Content"; \
  Flags: ignoreversion recursesubdirs createallsubdirs

; EULA in install dir for reference
Source: "..\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\HumHouse Drums X"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall HumHouse Drums X"; Filename: "{uninstallexe}"
Name: "{commondesktop}\HumHouse Drums X"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch HumHouse Drums X Standalone"; \
  Flags: nowait postinstall skipifsilent unchecked

[Messages]
WelcomeLabel2=This will install [name/ver] on your computer.%n%nThe VST3 plug-in will be placed in the standard location (Common Files\VST3) so FL Studio, Ableton, Reaper, Cubase, Studio One and Bitwig can find it on the next plugin scan.%n%nThe Standalone app will also be installed so you can run it without a DAW.%n%nIt is recommended that you close all other applications before continuing.
