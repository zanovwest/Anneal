; Anneal installer (Inno Setup 6). Built by `build.ps1 -Setup`, which passes the version from
; CMakeLists.txt and the build folder:
;     ISCC /DAppVersion=0.1.0 /DBuildDir=..\build /DOutDir=..\dist installer\Anneal.iss
;
; Installs the VST3 into the system VST3 folder (the one FL Studio scans), optionally the standalone
; app, and an uninstaller under Settings > Apps. Updating while a DAW has Anneal loaded works: the
; loaded binary is moved aside first (Windows allows that for a DLL in use) and the DAW picks up the
; new build the next time it loads the plugin. Nobody is asked to close anything.
;
; Silent update:  Anneal-x.y.z-Setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
; Testing without admin rights:  /CURRENTUSER /VST3DIR=<folder> /DIR=<folder>

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build"
#endif
#ifndef OutDir
  #define OutDir "..\dist"
#endif
#define Bundle BuildDir + "\Anneal_artefacts\Release\VST3\Anneal.vst3"
#define StandaloneExe BuildDir + "\Anneal_artefacts\Release\Standalone\Anneal.exe"

[Setup]
; Never change AppId: it is how an update finds the installed copy.
AppId={{146CC042-90A4-4E67-8055-FE35CBE64CD9}
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
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutDir}
OutputBaseFilename=Anneal-{#AppVersion}-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no
UninstallDisplayName=Anneal {#AppVersion} (VST3)
UninstallDisplayIcon={uninstallexe}
UsedUserAreasWarning=no
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
FinishedLabelNoIcons=Anneal is installed.%n%nFirst install: in FL Studio open Options > Manage plugins > Find installed plugins.%nUpdate: a DAW that has Anneal open uses the new version the next time it loads it.
FinishedLabel=Anneal is installed.%n%nFirst install: in FL Studio open Options > Manage plugins > Find installed plugins.%nUpdate: a DAW that has Anneal open uses the new version the next time it loads it.

[Components]
Name: "vst3"; Description: "Anneal VST3 plug-in"; Types: full compact custom; Flags: fixed
Name: "standalone"; Description: "Anneal standalone app (Start menu)"; Types: full

[Tasks]
Name: "peruser"; Description: "Also update the copy in your per-user VST3 folder (Cubase scans both folders)"; Check: PerUserCopyExists

[Files]
Source: "{#Bundle}\*"; DestDir: "{code:Vst3Dir}\Anneal.vst3"; Components: vst3; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#Bundle}\*"; DestDir: "{code:PerUserVst3Dir}\Anneal.vst3"; Tasks: peruser; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#StandaloneExe}"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
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

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then DeleteOldBinaries();
end;
