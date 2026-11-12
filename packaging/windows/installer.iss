; Multi-Stream Stats - Windows x64 installer (Inno Setup 6.7 or newer)
;
; Built by tools\package-windows.ps1 (local) and the Windows CI job; do not run ISCC by hand
; unless you pass the same defines:
;   ISCC /DAppVersion=1.1.0 /DPayloadDir=<folder with obs-multistream-stats.dll + data\>
;        /DOutputDir=<folder> /DLicenseFile=<LICENSE> [/DAppPublisher=...] [/DAppURL=https://github.com/<owner>/<repo>]
;        packaging\windows\installer.iss
;
; Where the plugin goes (OBS 33 plugin locations, see INSTALL.md):
;   Standard OBS (installer / Steam): C:\ProgramData\obs-studio\plugins\obs-multistream-stats\   (administrator)
;   Portable OBS (ZIP download):      <OBS folder>\plugins\obs-multistream-stats\                 (no administrator)
; Nothing else is written into OBS folders. Plugin settings (plugin_config\obs-multistream-stats) are never touched.
;
; Silent use (tests, IT deployment):
;   /VERYSILENT /SUPPRESSMSGBOXES [/ALLUSERS|/CURRENTUSER] /MODE=standard|portable [/OBSDIR="<portable OBS folder>"]
;   [/ALLOWDOWNGRADE] [/LOG="<file>"]
; Exit code 0 = installed; any other value = not installed (the reason is in the log).

#if VER < EncodeVer(6,3,0)
  #error Inno Setup 6.3 or newer is required (x64compatible architecture identifiers)
#endif
#pragma message "Inno Setup " + Str(VER >> 24) + "." + Str((VER >> 16) & 0xFF) + "." + Str((VER >> 8) & 0xFF)
#ifndef AppVersion
  #error Define AppVersion, e.g. ISCC /DAppVersion=1.1.0
#endif
#ifndef PayloadDir
  #error Define PayloadDir: the folder that contains obs-multistream-stats.dll and data\
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
#ifndef LicenseFile
  #define LicenseFile "..\..\LICENSE"
#endif
#ifndef AppPublisher
  #define AppPublisher "OBS Multi-Stream Stats Dock"
#endif
#ifndef AppURL
  #define AppURL ""
#endif

#define AppName "Multi-Stream Stats"
#define PluginName "obs-multistream-stats"
#define MinObsMajor "33"

[Setup]
; Never change AppId: it links upgrades and the uninstaller to earlier installs.
AppId={{9BF09CF6-FFE4-4C45-8478-282689049548}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
#if AppURL != ""
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
#endif
; The real target is chosen on the "Your OBS Studio" page and written into the (hidden) directory field.
DefaultDirName={commonappdata}\obs-studio\plugins\{#PluginName}
DisableDirPage=yes
DirExistsWarning=no
AppendDefaultDirName=no
UsePreviousAppDir=no
DisableProgramGroupPage=yes
; Administrator only for a standard OBS installation; portable OBS folders need no elevation.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile={#LicenseFile}
OutputDir={#OutputDir}
OutputBaseFilename={#PluginName}-{#AppVersion}-windows-x64-installer
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Never offer to close OBS automatically (it may be streaming); Setup asks the user instead.
CloseApplications=no
RestartApplications=no
; Keep the uninstaller out of the OBS plugin folder.
UninstallFilesDir={autoappdata}\{#AppName}\uninstall
UninstallDisplayName={#AppName}
SetupLogging=yes
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} {#AppVersion} installer
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}

[Messages]
WelcomeLabel2=This will install [name/ver] into your OBS Studio.%n%nThe plugin adds a Multi-Stream Statistics dock with live statistics for every streaming destination. It needs OBS Studio {#MinObsMajor} or newer (64-bit).%n%nPlease close OBS Studio before you continue.
PrivilegesRequiredOverrideTitle=How is OBS Studio installed?
PrivilegesRequiredOverrideInstruction=Choose how to run Setup
PrivilegesRequiredOverrideText1=A standard OBS Studio installation (from the OBS installer or Steam) keeps plugins in C:\ProgramData\obs-studio\plugins, which needs administrator rights.%n%nA portable OBS Studio folder (from the OBS ZIP download) needs no administrator rights.
PrivilegesRequiredOverrideText2=A standard OBS Studio installation (from the OBS installer or Steam) keeps plugins in C:\ProgramData\obs-studio\plugins, which needs administrator rights.%n%nA portable OBS Studio folder (from the OBS ZIP download) needs no administrator rights.
PrivilegesRequiredOverrideAllUsers=&With administrator rights (standard or portable OBS)
PrivilegesRequiredOverrideAllUsersRecommended=&With administrator rights (standard or portable OBS) - recommended
PrivilegesRequiredOverrideCurrentUser=With&out administrator rights (portable OBS folder only)
PrivilegesRequiredOverrideCurrentUserRecommended=With&out administrator rights (portable OBS folder only) - recommended
FinishedHeadingLabel=[name] is installed
FinishedLabelNoIcons=Start OBS Studio and open Docks > Multi-Stream Statistics.%n%nTo check the installation: Tools > Plugin Manager lists "Multi-Stream Statistics", and the OBS log (Help > Log Files) contains "[multistream-stats] loaded version {#AppVersion}".
FinishedLabel=Start OBS Studio and open Docks > Multi-Stream Statistics.

[CustomMessages]
ObsPageCaption=Your OBS Studio
ObsPageDescription=Choose the OBS Studio the plugin is for.
StandardChoice=&Standard OBS Studio (installed with the OBS installer or Steam)
StandardNeedsAdmin=Not available: restart Setup and choose "With administrator rights".
StandardTarget=Plugin folder: %1
PortableChoice=&Portable OBS Studio folder (from the OBS ZIP download)
PortableHint=Choose the OBS folder that contains the bin, data and config folders.
BrowseButton=B&rowse...
BrowsePrompt=Select your portable OBS Studio folder (the one that contains bin, data and config):
CloseObsNote=OBS Studio must be closed while the plugin is installed. Your OBS settings, scenes and profiles are not changed.
StandardFound=Found OBS Studio %1 at %2
StandardNotFound=No standard OBS Studio installation was found. The plugin can still be installed; OBS will load it once OBS Studio {#MinObsMajor} or newer is installed.
StandardNotFoundConfirm=No standard OBS Studio installation was found on this computer.%n%nInstall the plugin into %1 anyway?
NotObsFolder=This folder is not an OBS Studio folder:%n%1%n%nChoose the folder that contains bin\64bit\obs64.exe (usually named "OBS Studio" or "obs-studio").
ObsTooOld=This OBS Studio is version %1. The plugin needs OBS Studio {#MinObsMajor} or newer.%n%nUpdate OBS Studio first.
ObsNotX64=This OBS Studio (%1) is not the 64-bit x64 version (for example, it is the Windows on ARM version). This plugin build only works with x64 OBS Studio.
ObsVersionUnknown=The version of this OBS Studio could not be read (%1). Continue only if it is OBS Studio {#MinObsMajor} or newer.%n%nContinue?
NotPortableConfirm=This OBS folder is not set up as portable (it has no portable_mode.txt or obs_portable_mode.txt file):%n%1%n%nOBS only loads plugins from this folder when it is started with the --portable option. A standard OBS installation uses C:\ProgramData\obs-studio\plugins instead.%n%nInstall into this folder anyway?
DowngradeConfirm=Version %1 of the plugin is already installed here, which is newer than this Setup (%2).%n%nReplace it with %2?
ObsRunning=OBS Studio is running from the selected OBS folder (or Setup cannot tell which OBS is running).%n%nClose OBS Studio, then click Retry.
ObsRunningAbort=OBS Studio is running. Close OBS Studio and run Setup again.
ReadyObs=OBS Studio:
ReadyPlugin=Plugin folder:
ReadyAction=Action:
ReadyInstall=Install {#AppVersion}
ReadyUpgrade=Upgrade %1 to {#AppVersion}
ReadyReinstall=Reinstall {#AppVersion}
ReadyDowngrade=Replace newer %1 with {#AppVersion}
ReadySettings=Plugin settings are kept (plugin_config\obs-multistream-stats).
ReadyLegacy=Note: an older copy in a legacy OBS plugin folder was found and is NOT changed: %1. OBS skips it once this installation exists.
UninstallDone=The plugin files were removed.%n%nYour plugin settings were kept. To remove them as well, delete the folder plugin_config\obs-multistream-stats in your OBS configuration folder: AppData\Roaming\obs-studio for a standard OBS, or <OBS folder>\config\obs-studio for a portable OBS.
SilentNeedsMode=A silent installation needs /MODE=standard, or /MODE=portable together with /OBSDIR="<portable OBS folder>". Setup never guesses the OBS folder.
SilentStandardNeedsAdmin=/MODE=standard needs administrator rights (/ALLUSERS).
UninstallObsRunning=OBS Studio is running. Close OBS Studio, then click Retry.

[Files]
Source: "{#PayloadDir}\{#PluginName}.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; Debug symbols left by a manual or scripted install of an older version (they would not match the new DLL).
Type: files; Name: "{app}\{#PluginName}.pdb"
; Locale files are replaced as a set.
Type: files; Name: "{app}\data\locale\*.ini"

[UninstallDelete]
Type: dirifempty; Name: "{app}\data\locale"
Type: dirifempty; Name: "{app}\data"
Type: dirifempty; Name: "{app}"

[Code]
const
  PluginName = '{#PluginName}';
  MinObsMajor = {#MinObsMajor};
  MachineX64 = $8664;

var
  ObsPage: TWizardPage;
  RbStandard, RbPortable: TNewRadioButton;
  StandardInfo, PortableHint, CloseNote: TNewStaticText;
  PortableEdit: TNewEdit;
  PortableBrowse: TNewButton;
  SelMode: String;          { 'standard' or 'portable' }
  SelObsRoot: String;       { OBS root folder ('' if a standard OBS was not found) }
  SelObsVersion: String;
  SelTarget: String;        { plugin folder, becomes the app directory }
  ExistingVersion: String;  { '' = not installed yet }
  LegacyCopy: String;
  ParamError: String;       { invalid or missing command-line choice (silent installs) }

{ ---------- helpers ---------- }

function ObsExe(const Root: String): String;
begin
  Result := AddBackslash(Root) + 'bin\64bit\obs64.exe';
end;

function IsObsRoot(const Root: String): Boolean;
begin
  Result := (Root <> '') and FileExists(ObsExe(Root));
end;

{ Accepts the OBS folder itself, its bin or bin\64bit folder, or obs64.exe. }
function NormalizeRoot(Path: String): String;
begin
  Path := Trim(Path);
  StringChangeEx(Path, '/', '\', True);
  if (Length(Path) > 1) and (Path[1] = '"') and (Path[Length(Path)] = '"') then
    Path := Copy(Path, 2, Length(Path) - 2);
  Path := RemoveBackslashUnlessRoot(Path);
  if CompareText(ExtractFileName(Path), 'obs64.exe') = 0 then
    Path := ExtractFileDir(Path);
  if CompareText(ExtractFileName(Path), '64bit') = 0 then
    Path := ExtractFileDir(Path);
  if CompareText(ExtractFileName(Path), 'bin') = 0 then
    Path := ExtractFileDir(Path);
  Result := Path;
end;

function HasPortableMarker(const Root: String): Boolean;
var
  R: String;
begin
  R := AddBackslash(Root);
  Result := FileExists(R + 'portable_mode') or FileExists(R + 'obs_portable_mode') or
            FileExists(R + 'portable_mode.txt') or FileExists(R + 'obs_portable_mode.txt');
end;

function ObsMajor(const Exe: String): Integer;
var
  MS, LS: Cardinal;
begin
  if GetVersionNumbers(Exe, MS, LS) then
    Result := MS shr 16
  else
    Result := -1;
end;

function ObsVersionText(const Exe: String): String;
begin
  if not GetVersionNumbersString(Exe, Result) then
    Result := '?';
end;

{ PE machine type of an executable: $8664 = x64, $AA64 = ARM64, -1 = unreadable. }
function PeMachine(const FileName: String): Integer;
var
  Stream: TFileStream;
  Buf: AnsiString;
  Offset: Integer;
begin
  Result := -1;
  try
    Stream := TFileStream.Create(FileName, fmOpenRead or fmShareDenyNone);
    try
      SetLength(Buf, 4);
      Stream.Seek($3C, soFromBeginning);
      Stream.ReadBuffer(Buf, 4);
      Offset := Ord(Buf[1]) or (Ord(Buf[2]) shl 8) or (Ord(Buf[3]) shl 16) or (Ord(Buf[4]) shl 24);
      Stream.Seek(Offset + 4, soFromBeginning);
      SetLength(Buf, 2);
      Stream.ReadBuffer(Buf, 2);
      Result := Ord(Buf[1]) or (Ord(Buf[2]) shl 8);
    finally
      Stream.Free;
    end;
  except
    Result := -1;
  end;
end;

{ "version" from <plugin folder>\data\manifest.json; '' if absent. }
function ReadManifestVersion(const PluginDir: String): String;
var
  Raw: AnsiString;
  S: String;
  P: Integer;
begin
  Result := '';
  if not LoadStringFromFile(AddBackslash(PluginDir) + 'data\manifest.json', Raw) then begin
    if FileExists(AddBackslash(PluginDir) + PluginName + '.dll') then
      Result := 'unknown';
    Exit;
  end;
  S := String(Raw);
  P := Pos('"version"', S);
  if P = 0 then begin
    Result := 'unknown';
    Exit;
  end;
  Delete(S, 1, P + 8);
  P := Pos('"', S);
  if P = 0 then Exit;
  Delete(S, 1, P);
  P := Pos('"', S);
  if P = 0 then Exit;
  Result := Copy(S, 1, P - 1);
end;

{ True if a switch such as /ALLOWDOWNGRADE (or /ALLOWDOWNGRADE=1) was given on the command line. }
function HasSwitch(const Name: String): Boolean;
var
  I: Integer;
  P: String;
begin
  Result := False;
  for I := 1 to ParamCount do begin
    P := Lowercase(ParamStr(I));
    if (P = '/' + Lowercase(Name)) or (P = '/' + Lowercase(Name) + '=1') then begin
      Result := True;
      Exit;
    end;
  end;
end;

function NextVersionPart(var S: String): Integer;
var
  P: Integer;
begin
  P := Pos('.', S);
  if P = 0 then begin
    Result := StrToIntDef(S, 0);
    S := '';
  end else begin
    Result := StrToIntDef(Copy(S, 1, P - 1), 0);
    Delete(S, 1, P);
  end;
end;

function CompareVersions(A, B: String): Integer;
var
  I, X, Y: Integer;
begin
  Result := 0;
  for I := 1 to 4 do begin
    X := NextVersionPart(A);
    Y := NextVersionPart(B);
    if X < Y then begin
      Result := -1;
      Exit;
    end;
    if X > Y then begin
      Result := 1;
      Exit;
    end;
  end;
end;

function StandardPluginDir: String;
begin
  Result := ExpandConstant('{commonappdata}\obs-studio\plugins\') + PluginName;
end;

function FindStandardObsRoot: String;
var
  Dir: String;
begin
  Result := '';
  if RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Dir) and IsObsRoot(NormalizeRoot(Dir)) then begin
    Result := NormalizeRoot(Dir);
    Exit;
  end;
  if RegQueryStringValue(HKLM64, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio', 'InstallLocation', Dir) and
     IsObsRoot(NormalizeRoot(Dir)) then begin
    Result := NormalizeRoot(Dir);
    Exit;
  end;
  if IsObsRoot(ExpandConstant('{commonpf64}\obs-studio')) then begin
    Result := ExpandConstant('{commonpf64}\obs-studio');
    Exit;
  end;
  if RegQueryStringValue(HKCU, 'Software\Valve\Steam', 'SteamPath', Dir) then begin
    StringChangeEx(Dir, '/', '\', True);
    if IsObsRoot(AddBackslash(Dir) + 'steamapps\common\OBS Studio') then
      Result := AddBackslash(Dir) + 'steamapps\common\OBS Studio';
  end;
end;

function DetectPortableRoot: String;
begin
  Result := GetPreviousData('ObsRoot', '');
  if (GetPreviousData('Mode', '') = 'portable') and IsObsRoot(Result) then Exit;
  Result := ExpandConstant('{userdocs}\OBS Studio');
  if IsObsRoot(Result) then Exit;
  Result := ExpandConstant('{userdesktop}\OBS Studio');
  if IsObsRoot(Result) then Exit;
  Result := '';
end;

function OpenProcess(dwDesiredAccess: Cardinal; bInheritHandle: BOOL; dwProcessId: Cardinal): THandle;
  external 'OpenProcess@kernel32.dll stdcall';
function CloseHandle(hObject: THandle): BOOL;
  external 'CloseHandle@kernel32.dll stdcall';
function QueryFullProcessImageName(hProcess: THandle; dwFlags: Cardinal; lpExeName: String; var lpdwSize: Cardinal): BOOL;
  external 'QueryFullProcessImageNameW@kernel32.dll stdcall';

{ Image path of a process via PROCESS_QUERY_LIMITED_INFORMATION, which also works for an OBS that
  runs as administrator while Setup does not (WMI's ExecutablePath is empty in that case). }
function ProcessImagePath(Pid: Cardinal): String;
var
  H: THandle;
  Size: Cardinal;
  Buf: String;
begin
  Result := '';
  H := OpenProcess($1000, False, Pid);
  if H = 0 then Exit;
  try
    Size := 1024;
    SetLength(Buf, Size);
    if QueryFullProcessImageName(H, 0, Buf, Size) then
      Result := Copy(Buf, 1, Size);
  finally
    CloseHandle(H);
  end;
end;

{ Paths of running obs64.exe processes. Unknown = True if a process path could not be read. }
procedure GetRunningObs(var Paths: TArrayOfString; var Unknown: Boolean);
var
  Locator, Service, Procs, Proc: Variant;
  I, N: Integer;
  P: String;
begin
  SetArrayLength(Paths, 0);
  Unknown := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Procs := Service.ExecQuery('SELECT ProcessId, ExecutablePath FROM Win32_Process WHERE Name = ''obs64.exe''');
    N := Procs.Count;
    for I := 0 to N - 1 do begin
      Proc := Procs.ItemIndex(I);
      P := '';
      if not VarIsNull(Proc.ExecutablePath) then
        P := Proc.ExecutablePath;
      if P = '' then
        P := ProcessImagePath(Proc.ProcessId);
      if P = '' then
        Unknown := True
      else begin
        SetArrayLength(Paths, GetArrayLength(Paths) + 1);
        Paths[GetArrayLength(Paths) - 1] := P;
      end;
    end;
  except
    Log('Could not query running processes: ' + GetExceptionMessage);
    Unknown := True;
  end;
end;

{ True if an OBS that may load the plugin from PluginDir is running. }
function ObsBlocking(const PluginDir: String): Boolean;
var
  Paths: TArrayOfString;
  Unknown, IsStandardTarget: Boolean;
  I: Integer;
  ExeRoot, TargetRoot: String;
begin
  GetRunningObs(Paths, Unknown);
  Result := Unknown;
  if Unknown then
    Log('A running obs64.exe could not be identified; treating it as blocking.');
  IsStandardTarget := Pos(Lowercase(AddBackslash(ExpandConstant('{commonappdata}'))), Lowercase(PluginDir)) = 1;
  TargetRoot := ExtractFileDir(ExtractFileDir(PluginDir));
  for I := 0 to GetArrayLength(Paths) - 1 do begin
    ExeRoot := NormalizeRoot(Paths[I]);
    Log('Running OBS: ' + Paths[I]);
    if IsStandardTarget then begin
      if not HasPortableMarker(ExeRoot) then
        Result := True;
    end else if CompareText(RemoveBackslashUnlessRoot(ExeRoot), RemoveBackslashUnlessRoot(TargetRoot)) = 0 then
      Result := True;
  end;
end;

function FindLegacyCopy(const Mode, Root: String): String;
begin
  Result := '';
  if (Mode = 'portable') and FileExists(AddBackslash(Root) + 'obs-plugins\64bit\' + PluginName + '.dll') then
    Result := AddBackslash(Root) + 'obs-plugins\64bit\' + PluginName + '.dll'
  else if FileExists(StandardPluginDir + '\bin\64bit\' + PluginName + '.dll') then
    Result := StandardPluginDir + '\bin\64bit\' + PluginName + '.dll'
  else if (Mode = 'standard') and (Root <> '') and FileExists(AddBackslash(Root) + 'obs-plugins\64bit\' + PluginName + '.dll') then
    Result := AddBackslash(Root) + 'obs-plugins\64bit\' + PluginName + '.dll';
end;

{ ---------- page ---------- }

procedure UpdatePageState;
begin
  PortableEdit.Enabled := RbPortable.Checked;
  PortableBrowse.Enabled := RbPortable.Checked;
  PortableHint.Enabled := RbPortable.Checked;
end;

procedure ModeClick(Sender: TObject);
begin
  UpdatePageState;
end;

procedure BrowseClick(Sender: TObject);
var
  Dir: String;
begin
  Dir := PortableEdit.Text;
  if BrowseForFolder(CustomMessage('BrowsePrompt'), Dir, False) then
    PortableEdit.Text := NormalizeRoot(Dir);
end;

procedure InitializeWizard;
var
  Y: Integer;
  StdRoot, ParamMode, ParamDir: String;
begin
  ObsPage := CreateCustomPage(wpLicense, CustomMessage('ObsPageCaption'), CustomMessage('ObsPageDescription'));

  RbStandard := TNewRadioButton.Create(ObsPage);
  RbStandard.Parent := ObsPage.Surface;
  RbStandard.Caption := CustomMessage('StandardChoice');
  RbStandard.Left := 0;
  RbStandard.Top := 0;
  RbStandard.Width := ObsPage.SurfaceWidth;
  RbStandard.Height := ScaleY(20);
  RbStandard.OnClick := @ModeClick;
  Y := RbStandard.Top + RbStandard.Height + ScaleY(2);

  StandardInfo := TNewStaticText.Create(ObsPage);
  StandardInfo.Parent := ObsPage.Surface;
  StandardInfo.Left := ScaleX(18);
  StandardInfo.Top := Y;
  StandardInfo.Width := ObsPage.SurfaceWidth - ScaleX(18);
  StandardInfo.AutoSize := False;
  StandardInfo.WordWrap := True;
  StandardInfo.Height := ScaleY(44);
  StdRoot := FindStandardObsRoot;
  if not IsAdminInstallMode then
    StandardInfo.Caption := CustomMessage('StandardNeedsAdmin')
  else if StdRoot <> '' then
    StandardInfo.Caption := FmtMessage(CustomMessage('StandardFound'), [ObsVersionText(ObsExe(StdRoot)), StdRoot]) + #13#10 +
                            FmtMessage(CustomMessage('StandardTarget'), [StandardPluginDir])
  else
    StandardInfo.Caption := CustomMessage('StandardNotFound') + #13#10 + FmtMessage(CustomMessage('StandardTarget'), [StandardPluginDir]);
  Y := StandardInfo.Top + StandardInfo.Height + ScaleY(10);

  RbPortable := TNewRadioButton.Create(ObsPage);
  RbPortable.Parent := ObsPage.Surface;
  RbPortable.Caption := CustomMessage('PortableChoice');
  RbPortable.Left := 0;
  RbPortable.Top := Y;
  RbPortable.Width := ObsPage.SurfaceWidth;
  RbPortable.Height := ScaleY(20);
  RbPortable.OnClick := @ModeClick;
  Y := RbPortable.Top + RbPortable.Height + ScaleY(2);

  PortableHint := TNewStaticText.Create(ObsPage);
  PortableHint.Parent := ObsPage.Surface;
  PortableHint.Left := ScaleX(18);
  PortableHint.Top := Y;
  PortableHint.Caption := CustomMessage('PortableHint');
  Y := PortableHint.Top + PortableHint.Height + ScaleY(4);

  PortableBrowse := TNewButton.Create(ObsPage);
  PortableBrowse.Parent := ObsPage.Surface;
  PortableBrowse.Caption := CustomMessage('BrowseButton');
  PortableBrowse.Width := ScaleX(80);
  PortableBrowse.Height := ScaleY(23);
  PortableBrowse.Left := ObsPage.SurfaceWidth - PortableBrowse.Width;
  PortableBrowse.Top := Y;
  PortableBrowse.OnClick := @BrowseClick;

  PortableEdit := TNewEdit.Create(ObsPage);
  PortableEdit.Parent := ObsPage.Surface;
  PortableEdit.Left := ScaleX(18);
  PortableEdit.Top := Y + ScaleY(1);
  PortableEdit.Width := PortableBrowse.Left - ScaleX(26);
  PortableEdit.Text := DetectPortableRoot;
  Y := PortableBrowse.Top + PortableBrowse.Height + ScaleY(16);

  CloseNote := TNewStaticText.Create(ObsPage);
  CloseNote.Parent := ObsPage.Surface;
  CloseNote.Left := 0;
  CloseNote.Top := Y;
  CloseNote.Width := ObsPage.SurfaceWidth;
  CloseNote.AutoSize := False;
  CloseNote.WordWrap := True;
  CloseNote.Height := ScaleY(36);
  CloseNote.Caption := CustomMessage('CloseObsNote');

  { Default choice: a standard OBS if one exists, otherwise a detected portable folder. }
  if not IsAdminInstallMode then begin
    RbStandard.Enabled := False;
    RbPortable.Checked := True;
  end else if GetPreviousData('Mode', '') = 'portable' then
    RbPortable.Checked := True
  else if (StdRoot = '') and (PortableEdit.Text <> '') then
    RbPortable.Checked := True
  else
    RbStandard.Checked := True;

  { Command line: /MODE=standard|portable /OBSDIR=<folder> }
  ParamMode := Lowercase(ExpandConstant('{param:MODE|}'));
  ParamDir := ExpandConstant('{param:OBSDIR|}');
  if ParamDir <> '' then
    PortableEdit.Text := NormalizeRoot(ParamDir);
  ParamError := '';
  if ParamMode = 'standard' then begin
    if IsAdminInstallMode then
      RbStandard.Checked := True
    else
      ParamError := CustomMessage('SilentStandardNeedsAdmin');
  end else if ParamMode = 'portable' then begin
    RbPortable.Checked := True;
    if WizardSilent and (ParamDir = '') then
      ParamError := CustomMessage('SilentNeedsMode');
  end else if WizardSilent then
    { Interactive users see and confirm the preselected choice; a silent install must say what it wants. }
    ParamError := CustomMessage('SilentNeedsMode');

  UpdatePageState;
end;

{ Validates the choice on the OBS page and sets the target. Returns False to stay on the page. }
function ValidateObsChoice: Boolean;
var
  Root, Exe: String;
  Major, Machine: Integer;
begin
  Result := False;
  LegacyCopy := '';
  SelTarget := '';
  if ParamError <> '' then begin
    Log(ParamError);
    SuppressibleMsgBox(ParamError, mbError, MB_OK, IDOK);
    Exit;
  end;
  if RbStandard.Checked then begin
    if not IsAdminInstallMode then Exit;
    SelMode := 'standard';
    Root := FindStandardObsRoot;
    SelTarget := StandardPluginDir;
    if Root = '' then begin
      if SuppressibleMsgBox(FmtMessage(CustomMessage('StandardNotFoundConfirm'), [SelTarget]), mbConfirmation, MB_YESNO, IDYES) <> IDYES then
        Exit;
      SelObsVersion := '';
    end;
  end else begin
    SelMode := 'portable';
    Root := NormalizeRoot(PortableEdit.Text);
    PortableEdit.Text := Root;
    if not IsObsRoot(Root) then begin
      SuppressibleMsgBox(FmtMessage(CustomMessage('NotObsFolder'), [Root]), mbError, MB_OK, IDOK);
      Exit;
    end;
    SelTarget := AddBackslash(Root) + 'plugins\' + PluginName;
  end;

  if Root <> '' then begin
    Exe := ObsExe(Root);
    Machine := PeMachine(Exe);
    if (Machine <> -1) and (Machine <> MachineX64) then begin
      SuppressibleMsgBox(FmtMessage(CustomMessage('ObsNotX64'), [Exe]), mbError, MB_OK, IDOK);
      Exit;
    end;
    Major := ObsMajor(Exe);
    SelObsVersion := ObsVersionText(Exe);
    if Major = -1 then begin
      if SuppressibleMsgBox(FmtMessage(CustomMessage('ObsVersionUnknown'), [Exe]), mbConfirmation, MB_YESNO, IDNO) <> IDYES then
        Exit;
    end else if Major < MinObsMajor then begin
      SuppressibleMsgBox(FmtMessage(CustomMessage('ObsTooOld'), [SelObsVersion]), mbError, MB_OK, IDOK);
      Exit;
    end;
    if (SelMode = 'portable') and not HasPortableMarker(Root) then begin
      if SuppressibleMsgBox(FmtMessage(CustomMessage('NotPortableConfirm'), [Root]), mbConfirmation, MB_YESNO, IDNO) <> IDYES then
        Exit;
    end;
  end;
  SelObsRoot := Root;

  ExistingVersion := ReadManifestVersion(SelTarget);
  if (ExistingVersion <> '') and (ExistingVersion <> 'unknown') and (CompareVersions(ExistingVersion, '{#AppVersion}') > 0) then begin
    if not HasSwitch('ALLOWDOWNGRADE') and
       (SuppressibleMsgBox(FmtMessage(CustomMessage('DowngradeConfirm'), [ExistingVersion, '{#AppVersion}']), mbConfirmation, MB_YESNO, IDNO) <> IDYES) then
      Exit;
  end;

  while ObsBlocking(SelTarget) do begin
    if SuppressibleMsgBox(CustomMessage('ObsRunning'), mbError, MB_RETRYCANCEL, IDCANCEL) <> IDRETRY then
      Exit;
  end;

  LegacyCopy := FindLegacyCopy(SelMode, SelObsRoot);
  WizardForm.DirEdit.Text := SelTarget;
  { (A line must not start with "[" in an .iss file, even in [Code].) }
  Log(Format('Mode=%s OBS root=%s OBS version=%s target=%s existing=%s legacy=%s', [SelMode, SelObsRoot,
             SelObsVersion, SelTarget, ExistingVersion, LegacyCopy]));
  Result := True;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = ObsPage.ID then
    Result := ValidateObsChoice;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  { Normally validated by NextButtonClick (also called in silent mode); validate again if not. }
  if (SelTarget = '') and not ValidateObsChoice then begin
    Result := 'No valid OBS Studio was selected. See the Setup log for details.';
    Exit;
  end;
  if ObsBlocking(SelTarget) then
    Result := CustomMessage('ObsRunningAbort');
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo,
  MemoGroupInfo, MemoTasksInfo: String): String;
var
  Action, ObsText: String;
begin
  if ExistingVersion = '' then
    Action := CustomMessage('ReadyInstall')
  else if ExistingVersion = '{#AppVersion}' then
    Action := CustomMessage('ReadyReinstall')
  else if (ExistingVersion <> 'unknown') and (CompareVersions(ExistingVersion, '{#AppVersion}') > 0) then
    Action := FmtMessage(CustomMessage('ReadyDowngrade'), [ExistingVersion])
  else
    Action := FmtMessage(CustomMessage('ReadyUpgrade'), [ExistingVersion]);

  if SelObsRoot <> '' then
    ObsText := SelObsRoot + ' (' + SelObsVersion + ', ' + SelMode + ')'
  else
    ObsText := '(' + SelMode + ', not found on this computer)';

  Result := CustomMessage('ReadyObs') + NewLine + Space + ObsText + NewLine + NewLine +
            CustomMessage('ReadyPlugin') + NewLine + Space + SelTarget + NewLine + NewLine +
            CustomMessage('ReadyAction') + NewLine + Space + Action + NewLine + Space + CustomMessage('ReadySettings');
  if LegacyCopy <> '' then
    Result := Result + NewLine + NewLine + FmtMessage(CustomMessage('ReadyLegacy'), [LegacyCopy]);
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'Mode', SelMode);
  SetPreviousData(PreviousDataKey, 'ObsRoot', SelObsRoot);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    Log(Format('Installed %s %s into %s (previous: %s)', [PluginName, '{#AppVersion}', SelTarget, ExistingVersion]));
end;

{ ---------- uninstall ---------- }

function InitializeUninstall: Boolean;
var
  App: String;
begin
  Result := True;
  App := ExpandConstant('{app}');
  while ObsBlocking(App) do begin
    if UninstallSilent then begin
      Log('OBS Studio is running; uninstall cancelled.');
      Result := False;
      Exit;
    end;
    if MsgBox(CustomMessage('UninstallObsRunning'), mbError, MB_RETRYCANCEL) <> IDRETRY then begin
      Result := False;
      Exit;
    end;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and not UninstallSilent then
    MsgBox(CustomMessage('UninstallDone'), mbInformation, MB_OK);
end;
