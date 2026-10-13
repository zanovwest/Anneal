; Anneal installer (Inno Setup 6). Built by `build.ps1 -Setup`, which passes the version from
; CMakeLists.txt and the build folder:
;     ISCC /DAppVersion=0.1.0 /DBuildDir=..\build /DOutDir=..\dist installer\Anneal.iss
;
; Two install modes (Setup asks, or pass /CURRENTUSER or /ALLUSERS):
;   just for me  - no administrator rights. VST3 into %LOCALAPPDATA%\Programs\Common\VST3, the app into
;                  %LOCALAPPDATA%\Programs\Anneal. Updates never ask for approval, so they can run
;                  unattended. FL Studio needs that folder added once under Plugin search paths.
;   all users    - administrator approval. VST3 into C:\Program Files\Common Files\VST3 (FL Studio's
;                  default folder), the app into Program Files.
; Plus an uninstaller under Settings > Apps. Updating while a DAW has Anneal loaded works: the loaded
; binary is moved aside first (Windows allows that for a DLL in use) and the DAW picks up the new build
; the next time it loads the plugin. Nobody is asked to close anything.
;
; Unattended update:  Anneal-x.y.z-Setup.exe /CURRENTUSER /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
; Sandbox test (build.ps1 builds it with its own AppId):  /CURRENTUSER /VST3DIR=<folder> /DIR=<folder>

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build"
#endif
#ifndef OutDir
  #define OutDir "..\dist"
#endif
; Never change the real AppId: it is how an update finds the installed copy.
#ifndef AppIdValue
  #define AppIdValue "{{146CC042-90A4-4E67-8055-FE35CBE64CD9}"
#endif
#ifndef OutputName
  #define OutputName "Anneal-" + AppVersion + "-Setup"
#endif
#define Bundle BuildDir + "\Anneal_artefacts\Release\VST3\Anneal.vst3"
#define StandaloneExe BuildDir + "\Anneal_artefacts\Release\Standalone\Anneal.exe"

[Setup]
AppId={#AppIdValue}
AppName=Anneal
AppVersion={#AppVersion}
AppVerName=Anneal {#AppVersion}
AppPublisher=Qulic
AppPublisherURL=https://github.com/zanovwest/Anneal
VersionInfoVersion={#AppVersion}
VersionInfoDescription=Anneal setup
DefaultDirName={autopf}\Anneal
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutDir}
OutputBaseFilename={#OutputName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no
UninstallDisplayName=Anneal {#AppVersion} (VST3)
SetupIconFile=..\resources\icon\anneal.ico
UninstallDisplayIcon={app}\anneal.ico
UsedUserAreasWarning=no
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Components]
Name: "vst3"; Description: "Anneal VST3 plug-in"; Types: full compact custom; Flags: fixed
Name: "standalone"; Description: "Anneal standalone app (Start menu)"; Types: full

[Tasks]
Name: "peruser"; Description: "Also update the copy in your per-user VST3 folder (Cubase scans both folders)"; Check: PerUserCopyExists

; Explorer shows the bundle's Plugin.ico only on a folder marked "system" with a hidden, system
; desktop.ini; a plain copy loses both attributes, so they are set here.
[Dirs]
Name: "{code:Vst3Dir}\Anneal.vst3"; Attribs: system; Components: vst3
Name: "{code:PerUserVst3Dir}\Anneal.vst3"; Attribs: system; Tasks: peruser

[Files]
Source: "{#Bundle}\*"; Excludes: "desktop.ini"; DestDir: "{code:Vst3Dir}\Anneal.vst3"; Components: vst3; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#Bundle}\desktop.ini"; DestDir: "{code:Vst3Dir}\Anneal.vst3"; Attribs: hidden system; Components: vst3; Flags: ignoreversion overwritereadonly uninsremovereadonly
Source: "{#Bundle}\*"; Excludes: "desktop.ini"; DestDir: "{code:PerUserVst3Dir}\Anneal.vst3"; Tasks: peruser; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#Bundle}\desktop.ini"; DestDir: "{code:PerUserVst3Dir}\Anneal.vst3"; Attribs: hidden system; Tasks: peruser; Flags: ignoreversion overwritereadonly uninsremovereadonly
Source: "{#StandaloneExe}"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\resources\icon\anneal.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\CHANGELOG.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Anneal"; Filename: "{app}\Anneal.exe"; Components: standalone

; Bundle folders that existed before the first setup (earlier manual copies) are not Setup's own,
; so remove them explicitly, and only when empty.
[UninstallDelete]
Type: dirifempty; Name: "{code:Vst3Dir}\Anneal.vst3\Contents\x86_64-win"
Type: dirifempty; Name: "{code:Vst3Dir}\Anneal.vst3\Contents\Resources"
Type: dirifempty; Name: "{code:Vst3Dir}\Anneal.vst3\Contents"
Type: dirifempty; Name: "{code:Vst3Dir}\Anneal.vst3"
Type: dirifempty; Name: "{code:PerUserVst3Dir}\Anneal.vst3\Contents\x86_64-win"; Tasks: peruser
Type: dirifempty; Name: "{code:PerUserVst3Dir}\Anneal.vst3\Contents\Resources"; Tasks: peruser
Type: dirifempty; Name: "{code:PerUserVst3Dir}\Anneal.vst3\Contents"; Tasks: peruser
Type: dirifempty; Name: "{code:PerUserVst3Dir}\Anneal.vst3"; Tasks: peruser

[Code]
const
  BinRel = '\Anneal.vst3\Contents\x86_64-win\Anneal.vst3';

function Vst3Dir(Param: String): String;
begin
  if IsAdminInstallMode then
    Result := ExpandConstant('{commoncf64}\VST3')
  else
    Result := ExpandConstant('{localappdata}\Programs\Common\VST3');
  Result := ExpandConstant('{param:VST3DIR|' + Result + '}');
end;

function PerUserVst3Dir(Param: String): String;
begin
  Result := ExpandConstant('{localappdata}\Programs\Common\VST3');
end;

function PerUserCopyExists(): Boolean;
begin
  Result := IsAdminInstallMode and
            (CompareText(PerUserVst3Dir(''), Vst3Dir('')) <> 0) and
            FileExists(PerUserVst3Dir('') + BinRel);
end;

// Binaries moved aside by earlier updates wait here until no DAW holds them.
function OldDir(): String;
begin
  Result := ExpandConstant('{%TEMP}\AnnealOld');
end;

procedure DeleteOldBinaries();
var
  F: TFindRec;
begin
  if FindFirst(OldDir() + '\*', F) then
  try
    repeat
      if (F.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then
        DeleteFile(OldDir() + '\' + F.Name);
    until not FindNext(F);
  finally
    FindClose(F);
  end;
end;

procedure MoveAside(const Dir: String);
var
  Bin, Dest: String;
begin
  Bin := Dir + BinRel;
  if not FileExists(Bin) then Exit;
  if DeleteFile(Bin) then Exit;   // not in use
  ForceDirectories(OldDir());
  Dest := OldDir() + '\Anneal-' + GetDateTimeString('yyyymmddhhnnss', #0, #0) + '-' + IntToStr(Random(100000)) + '.vst3';
  if not RenameFile(Bin, Dest) then
    if not RenameFile(Bin, Bin + '.old-' + GetDateTimeString('yyyymmddhhnnss', #0, #0)) then
      Log('Could not move aside ' + Bin);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    DeleteOldBinaries();
    if WizardIsComponentSelected('vst3') then MoveAside(Vst3Dir(''));
    if WizardIsTaskSelected('peruser') then MoveAside(PerUserVst3Dir(''));
  end;
end;

function AllUsersKeyExists(): Boolean;
begin
  Result := RegKeyExists(HKLM64, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\' +
                         '{#StringChange(AppIdValue, "{{", "{")}_is1');
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  if (not IsAdminInstallMode) and AllUsersKeyExists() then
  begin
    Log('An all-users Anneal is also installed; DAWs may load that copy.');
    if not WizardSilent() then
      MsgBox('Anneal is also installed for all users (C:\Program Files\Common Files\VST3).' + #13#10 +
             'Your DAW may keep loading that copy. Remove it under Settings > Apps > Anneal, ' +
             'then add your per-user VST3 folder in FL Studio.', mbInformation, MB_OK);
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
var
  First: String;
begin
  if CurPageID <> wpFinished then Exit;
  if IsAdminInstallMode then
    First := 'First install: in FL Studio open Options > Manage plugins > Find installed plugins.'
  else
    First := 'First install: in FL Studio open Options > Manage plugins, add this folder under ' +
             'Plugin search paths, then click Find installed plugins.';
  WizardForm.FinishedLabel.Caption := 'Anneal is installed in' + #13#10 + Vst3Dir('') + #13#10#13#10 +
    First + #13#10#13#10 + 'Update: a DAW that has Anneal open uses the new version the next time it loads it.';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then DeleteOldBinaries();
end;
