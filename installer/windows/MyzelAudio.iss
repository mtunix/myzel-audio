; Myzel Audio suite installer for Windows (installer/PLAN.md). Names no plugin: build.sh generates
; the per-plugin parts (GenDir\plugins.iss) from installer/plugins.tsv and the stage.
; Extra parameters: /REMOVE=<ids> removes just those plugins (with /COMPONENTS it ticks their
; Remove rows instead); /LIST=<file> writes the detection table and installs nothing.

#ifndef SuiteVersion
  #error Build with installer/windows/build.sh: it defines SuiteVersion, StageDir, GenDir, RepoRoot and the plugin versions.
#endif

#define Vendor "Myzel Audio"
; Frozen (installer/PLAN.md): updates and the suite uninstaller find earlier installs by it.
#define AppGuid "0AFB3883-05BD-477F-B777-E539AC381E0E"

[Setup]
AppId={{{#AppGuid}}
AppName={#Vendor}
AppVersion={#SuiteVersion}
AppVerName={#Vendor} {#SuiteVersion}
AppPublisher={#Vendor}
AppPublisherURL=https://github.com/mtunix/myzel-audio
AppSupportURL=https://github.com/mtunix/myzel-audio/issues
UninstallDisplayName={#Vendor}
VersionInfoVersion={#SuiteVersion}
VersionInfoCompany={#Vendor}
VersionInfoDescription={#Vendor} plugins setup
DefaultDirName={autopf}\{#Vendor}
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableWelcomePage=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
; The default filter (*.exe,*.dll,*.chm) would skip the plugin binaries a host holds open.
CloseApplicationsFilter=*.exe,*.dll,*.vst3,*.clap
RestartApplications=no
LicenseFile={#RepoRoot}\LICENSE
; Ticks come from detection, not from the last run.
UsePreviousSetupType=no
UsePreviousTasks=no
ShowComponentSizes=no
WizardStyle=modern
WizardSizePercent=130
SetupLogging=yes
Compression=lzma2/max
SolidCompression=yes
OutputBaseFilename=MyzelAudio-{#SuiteVersion}-windows-x64-setup

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel2=This installs, updates or removes the {#Vendor} plugins (suite {#SuiteVersion}) as VST3, CLAP and standalone apps.%n%nThe next pages show what is already on this computer. The plugins are free software under the GNU Affero General Public License v3.
SelectComponentsDesc=Which plugins should be installed, updated or removed?
SelectComponentsLabel2=Tick the plugins to install or update. Unticking a plugin leaves it as it is; to delete one, tick it under Remove.
SelectTasksDesc=Which formats should be installed?
SelectTasksLabel2=The formats apply to every plugin ticked on the previous page.
FinishedHeadingLabel=Finished

[Tasks]
Name: "vst3"; Description: "VST3"; GroupDescription: "Formats:"
Name: "clap"; Description: "CLAP"; GroupDescription: "Formats:"
Name: "standalone"; Description: "Standalone app, with a Start menu entry"; GroupDescription: "Formats:"

[Files]
; The maintenance copy that the per-plugin Settings -> Apps entries run with /REMOVE.
Source: "{srcexe}"; DestDir: "{app}"; DestName: "MyzelAudioSetup.exe"; Flags: external ignoreversion; Check: NotRunningMaintenanceCopy

#include AddBackslash(GenDir) + "plugins.iss"

[Code]
const
  VendorKey = 'Software\{#Vendor}';
  AppsKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall';
  AppsPrefix = 'MyzelAudio_';
  MaintenanceExe = 'MyzelAudioSetup.exe';
  StNotInstalled = 0;
  StOlder = 1;
  StUnknown = 2;
  StSame = 3;
  StNewer = 4;
  StNotOffered = 5;
  RemoveFailedExitCode = 9;

var
  // One entry per plugin in this installer (component order), then plugins only found by a receipt.
  PId, PName, PVersion, PFormats, PFound, PFoundFormats: array of String;
  PStatus: array of Integer;
  PCount: Integer;
  AppDir: String;
  RemoveList: TNewCheckListBox;
  RemoveRow: array of Integer;
  RemoveFailed: String;
  UninstallSuiteAtExit: Boolean;

procedure ExitProcess(ExitCode: Cardinal);
  external 'ExitProcess@kernel32.dll stdcall';

function SuiteAppsKey: String;
begin
  Result := AppsKey + '\{' + '{#AppGuid}' + '}_is1';
end;

function SplitNext(var S: String): String;
var
  P: Integer;
begin
  P := Pos(',', S);
  if P = 0 then
  begin
    Result := Trim(S);
    S := '';
  end
  else
  begin
    Result := Trim(Copy(S, 1, P - 1));
    S := Copy(S, P + 1, Length(S));
  end;
end;

function InList(const List, Item: String): Boolean;
begin
  Result := Pos(',' + Lowercase(Item) + ',', ',' + Lowercase(List) + ',') > 0;
end;

function CompareVersions(A, B: String): Integer;
var
  X, Y: Integer;
begin
  Result := 0;
  StringChangeEx(A, '.', ',', True);
  StringChangeEx(B, '.', ',', True);
  while (Result = 0) and ((A <> '') or (B <> '')) do
  begin
    X := StrToIntDef(SplitNext(A), 0);
    Y := StrToIntDef(SplitNext(B), 0);
    if X < Y then
      Result := -1
    else if X > Y then
      Result := 1;
  end;
end;

function Vst3Dir(const Name: String): String;
begin
  Result := ExpandConstant('{commoncf64}\VST3\') + Name + '.vst3';
end;

function Vst3Binary(const Name: String): String;
begin
  Result := Vst3Dir(Name) + '\Contents\x86_64-win\' + Name + '.vst3';
end;

function ClapFile(const Name: String): String;
begin
  Result := ExpandConstant('{commoncf64}\CLAP\') + Name + '.clap';
end;

function StandaloneExe(const Dir, Name: String): String;
begin
  Result := AddBackslash(Dir) + Name + '.exe';
end;

function StartMenuDir: String;
begin
  Result := ExpandConstant('{autoprograms}\{#Vendor}');
end;

function FormatsOnDisk(const Name, Dir: String): String;
begin
  Result := '';
  if DirExists(Vst3Dir(Name)) then
    Result := 'VST3';
  if FileExists(ClapFile(Name)) then
    Result := Result + ',CLAP';
  if FileExists(StandaloneExe(Dir, Name)) then
    Result := Result + ',Standalone';
  if Copy(Result, 1, 1) = ',' then
    Result := Copy(Result, 2, Length(Result));
end;

function ReadableList(S: String): String;
begin
  StringChangeEx(S, ',', ', ', True);
  Result := S;
end;

// JUCE stamps VERSIONINFO as a.b.c.0; plugin versions have three parts.
function FileVersion(const FileName: String): String;
var
  Major, Minor, Revision, Build: Word;
begin
  Result := '';
  if FileExists(FileName) and GetVersionComponents(FileName, Major, Minor, Revision, Build) then
  begin
    Result := IntToStr(Major) + '.' + IntToStr(Minor) + '.' + IntToStr(Revision);
    if Build <> 0 then
      Result := Result + '.' + IntToStr(Build);
  end;
end;

function DetectAppDir: String;
begin
  if not RegQueryStringValue(HKLM64, SuiteAppsKey, 'Inno Setup: App Path', Result) or (Result = '') then
    Result := ExpandConstant('{commonpf64}\{#Vendor}');
end;

function FindPlugin(const Key: String; ById: Boolean): Integer;
var
  I: Integer;
begin
  Result := -1;
  for I := 0 to PCount - 1 do
    if (ById and (CompareText(PId[I], Key) = 0)) or ((not ById) and (CompareText(PName[I], Key) = 0)) then
    begin
      Result := I;
      exit;
    end;
end;

procedure AddPlugin(const Id, Name, Version, Formats: String);
begin
  PCount := PCount + 1;
  SetArrayLength(PId, PCount);
  SetArrayLength(PName, PCount);
  SetArrayLength(PVersion, PCount);
  SetArrayLength(PFormats, PCount);
  SetArrayLength(PFound, PCount);
  SetArrayLength(PFoundFormats, PCount);
  SetArrayLength(PStatus, PCount);
  PId[PCount - 1] := Id;
  PName[PCount - 1] := Name;
  PVersion[PCount - 1] := Version;
  PFormats[PCount - 1] := Formats;
end;

// Receipt first, then the files on disk (installer/PLAN.md, Detection).
procedure Detect(I: Integer);
var
  Key, V, F: String;
  HasReceipt: Boolean;
begin
  Key := VendorKey + '\' + PName[I];
  HasReceipt := RegQueryStringValue(HKLM64, Key, 'Version', V);
  // The files are the truth: a receipt whose files were deleted by hand is stale (ForgetStaleReceipt).
  F := FormatsOnDisk(PName[I], AppDir);
  if F = '' then
  begin
    PStatus[I] := StNotInstalled;
    exit;
  end;
  if not HasReceipt then
    V := FileVersion(Vst3Binary(PName[I]));
  PFound[I] := V;
  PFoundFormats[I] := F;
  if V = '' then
    PStatus[I] := StUnknown
  else if PVersion[I] = '' then
    PStatus[I] := StNotOffered
  else
    case CompareVersions(V, PVersion[I]) of
      -1: PStatus[I] := StOlder;
      0: PStatus[I] := StSame;
    else
      PStatus[I] := StNewer;
    end;
end;

procedure LoadPlugins;
var
  I: Integer;
  Id, Name, Version, Formats: String;
  Names: TArrayOfString;
begin
  AppDir := DetectAppDir;
  for I := 0 to PluginCount - 1 do
  begin
    GetPlugin(I, Id, Name, Version, Formats);
    AddPlugin(Id, Name, Version, Formats);
  end;
  if RegGetSubkeyNames(HKLM64, VendorKey, Names) then
    for I := 0 to GetArrayLength(Names) - 1 do
      if FindPlugin(Names[I], False) < 0 then
      begin
        if not RegQueryStringValue(HKLM64, VendorKey + '\' + Names[I], 'Id', Id) or (Id = '') then
          Id := Lowercase(Names[I]);
        AddPlugin(Id, Names[I], '', '');
      end;
  for I := 0 to PCount - 1 do
    Detect(I);
end;

function IsInstalled(I: Integer): Boolean;
begin
  Result := PStatus[I] <> StNotInstalled;
end;

function StatusKey(I: Integer): String;
begin
  case PStatus[I] of
    StNotInstalled: Result := 'notinstalled';
    StOlder: Result := 'older';
    StUnknown: Result := 'unknown';
    StSame: Result := 'current';
    StNewer: Result := 'newer';
  else
    Result := 'notoffered';
  end;
end;

function StatusText(I: Integer): String;
begin
  case PStatus[I] of
    StNotInstalled: Result := 'not installed';
    StOlder: Result := 'installed ' + PFound[I] + ' ' + #$2192 + ' updates to ' + PVersion[I];
    StUnknown:
      if PVersion[I] = '' then
        Result := 'installed (unknown version)'
      else
        Result := 'installed (unknown version) ' + #$2192 + ' replaces with ' + PVersion[I];
    StSame: Result := 'installed ' + PFound[I] + ' (up to date)';
    StNewer: Result := 'installed ' + PFound[I] + ' (newer)';
  else
    Result := 'installed ' + PFound[I];
  end;
end;

function TickedByDefault(I: Integer): Boolean;
begin
  Result := (PStatus[I] = StNotInstalled) or (PStatus[I] = StOlder) or (PStatus[I] = StUnknown);
end;

function InstalledLabel(I: Integer): String;
begin
  if PFound[I] = '' then
    Result := PName[I] + ' (unknown version)'
  else
    Result := PName[I] + ' ' + PFound[I];
end;

// Deletes every format of one plugin, its receipt and its Settings -> Apps entry.
function RemovePluginFiles(const Name, Id, Dir: String): Boolean;
begin
  DelTree(Vst3Dir(Name), True, True, True);
  DeleteFile(ClapFile(Name));
  DeleteFile(StandaloneExe(Dir, Name));
  DeleteFile(StartMenuDir + '\' + Name + '.lnk');
  RemoveDir(StartMenuDir);
  Result := not DirExists(Vst3Dir(Name)) and not FileExists(ClapFile(Name)) and not FileExists(StandaloneExe(Dir, Name));
  if Result then
  begin
    RegDeleteKeyIncludingSubkeys(HKLM64, VendorKey + '\' + Name);
    RegDeleteKeyIncludingSubkeys(HKLM64, AppsKey + '\' + AppsPrefix + Id);
    RegDeleteKeyIfEmpty(HKLM64, VendorKey);
    Log('Removed ' + Name);
  end
  else
    Log('Could not remove every file of ' + Name + ' (in use by a host?)');
end;

function AnyReceipt: Boolean;
var
  Names: TArrayOfString;
begin
  Result := RegGetSubkeyNames(HKLM64, VendorKey, Names) and (GetArrayLength(Names) > 0);
end;

// A receipt with no file left would keep a phantom Apps entry and the suite alive: forget it.
procedure ForgetStaleReceipt(I: Integer);
begin
  if (PStatus[I] = StNotInstalled) and RegKeyExists(HKLM64, VendorKey + '\' + PName[I]) then
  begin
    RegDeleteKeyIncludingSubkeys(HKLM64, VendorKey + '\' + PName[I]);
    RegDeleteKeyIncludingSubkeys(HKLM64, AppsKey + '\' + AppsPrefix + PId[I]);
    RegDeleteKeyIfEmpty(HKLM64, VendorKey);
    Log('Forgot the receipt of ' + PName[I] + ': none of its files is left');
  end;
end;

// Runs after this process has exited, so the uninstaller can delete the maintenance copy.
procedure LaunchSuiteUninstaller;
var
  Uninstaller: String;
  Code: Integer;
begin
  if not RegQueryStringValue(HKLM64, SuiteAppsKey, 'UninstallString', Uninstaller) then
    exit;
  Log('No plugin left: running the suite uninstaller ' + Uninstaller);
  Exec(ExpandConstant('{cmd}'), '/C ping -n 4 127.0.0.1 >NUL & ' + Uninstaller + ' /VERYSILENT /SUPPRESSMSGBOXES /NORESTART',
    '', SW_HIDE, ewNoWait, Code);
end;

procedure ExitNow(Code: Integer);
begin
  DelTree(ExpandConstant('{tmp}'), True, True, True);
  ExitProcess(Code);
end;

// /LIST=<file>: the detection table, one tab-separated line per plugin; installs nothing.
procedure WriteList(const FileName: String);
var
  Lines: TArrayOfString;
  I: Integer;
begin
  SetArrayLength(Lines, PCount + 1);
  Lines[0] := '# id' + #9 + 'name' + #9 + 'status' + #9 + 'installed' + #9 + 'formats' + #9 + 'offered' + #9 + 'text';
  for I := 0 to PCount - 1 do
    Lines[I + 1] := PId[I] + #9 + PName[I] + #9 + StatusKey(I) + #9 + PFound[I] + #9 + PFoundFormats[I] + #9 + PVersion[I] + #9 + StatusText(I);
  if SaveStringsToUTF8File(FileName, Lines, False) then
    ExitNow(0)
  else
    ExitNow(1);
end;

// /REMOVE=<ids> without /COMPONENTS: removes just those plugins, then exits.
procedure RemoveOnly(Ids: String);
var
  Id: String;
  I, Code: Integer;
begin
  Code := 0;
  while Ids <> '' do
  begin
    Id := SplitNext(Ids);
    if Id = '' then
      continue;
    I := FindPlugin(Id, True);
    if I < 0 then
    begin
      Log('/REMOVE: unknown plugin ' + Id);
      SuppressibleMsgBox('No {#Vendor} plugin "' + Id + '" is known to this setup.', mbError, MB_OK, IDOK);
      Code := 1;
    end
    else if not IsInstalled(I) then
      Log('/REMOVE: ' + PName[I] + ' is not installed')
    else if not RemovePluginFiles(PName[I], PId[I], AppDir) then
    begin
      SuppressibleMsgBox(PName[I] + ' could not be removed completely. Close every host that uses it and try again.', mbError, MB_OK, IDOK);
      Code := RemoveFailedExitCode;
    end;
  end;
  for I := 0 to PCount - 1 do
    ForgetStaleReceipt(I);
  if (Code = 0) and not AnyReceipt then
    LaunchSuiteUninstaller;
  ExitNow(Code);
end;

function ComponentsGiven: Boolean;
begin
  Result := ExpandConstant('{param:COMPONENTS|*absent*}') <> '*absent*';
end;

function InitializeSetup: Boolean;
var
  ListFile, RemoveIds: String;
begin
  Result := True;
  LoadPlugins;
  ListFile := ExpandConstant('{param:LIST|}');
  if ListFile <> '' then
    WriteList(ListFile);
  RemoveIds := ExpandConstant('{param:REMOVE|}');
  if (RemoveIds <> '') and not ComponentsGiven then
    RemoveOnly(RemoveIds);
end;

function NotRunningMaintenanceCopy: Boolean;
begin
  Result := CompareText(ExpandConstant('{srcexe}'), ExpandConstant('{app}\') + MaintenanceExe) <> 0;
end;

function IsRemoving(I: Integer): Boolean;
var
  Row: Integer;
begin
  Result := False;
  if RemoveList <> nil then
    for Row := 0 to GetArrayLength(RemoveRow) - 1 do
      if (RemoveRow[Row] = I) and RemoveList.Checked[Row] then
        Result := True;
end;

function IsInstalling(I: Integer): Boolean;
begin
  Result := (I < PluginCount) and WizardIsComponentSelected(PId[I]) and not IsRemoving(I);
end;

// Ticking a remove row unticks and disables that plugin's install row.
procedure SyncRemoveRows(Sender: TObject);
var
  I: Integer;
begin
  for I := 0 to PluginCount - 1 do
  begin
    if IsRemoving(I) and WizardIsComponentSelected(PId[I]) then
      WizardSelectComponents('!' + PId[I]);
    WizardForm.ComponentsList.ItemEnabled[I] := not IsRemoving(I);
  end;
end;

procedure ShowStatus;
var
  I: Integer;
begin
  for I := 0 to PluginCount - 1 do
    WizardForm.ComponentsList.ItemSubItem[I] := StatusText(I);
end;

procedure CreateRemoveGroup;
var
  List: TNewCheckListBox;
  RemoveLabel: TNewStaticText;
  I, Row, Rows, Gap: Integer;
begin
  Rows := 0;
  for I := 0 to PCount - 1 do
    if IsInstalled(I) then
      Rows := Rows + 1;
  List := WizardForm.ComponentsList;
  // Setup types mean nothing here: the ticks come from detection.
  WizardForm.TypesCombo.Visible := False;
  List.Height := List.Top + List.Height - WizardForm.TypesCombo.Top;
  List.Top := WizardForm.TypesCombo.Top;
  if Rows = 0 then
    exit;
  Gap := ScaleY(8);

  RemoveLabel := TNewStaticText.Create(WizardForm);
  RemoveLabel.Parent := List.Parent;
  RemoveLabel.Caption := 'Remove (deletes every format of the plugin):';
  RemoveLabel.Left := List.Left;

  RemoveList := TNewCheckListBox.Create(WizardForm);
  RemoveList.Parent := List.Parent;
  RemoveList.Left := List.Left;
  RemoveList.Width := List.Width;
  RemoveList.Height := Rows * ScaleY(20) + ScaleY(6);
  RemoveList.Flat := List.Flat;

  List.Height := List.Height - (Gap + RemoveLabel.Height + ScaleY(4) + RemoveList.Height);
  RemoveLabel.Top := List.Top + List.Height + Gap;
  RemoveList.Top := RemoveLabel.Top + RemoveLabel.Height + ScaleY(4);
  RemoveLabel.Anchors := [akLeft, akBottom];
  RemoveList.Anchors := [akLeft, akRight, akBottom];

  SetArrayLength(RemoveRow, Rows);
  for I := 0 to PCount - 1 do
    if IsInstalled(I) then
    begin
      Row := RemoveList.AddCheckBox(InstalledLabel(I), ReadableList(PFoundFormats[I]), 0, False, True, False, False, nil);
      RemoveRow[Row] := I;
    end;
  RemoveList.OnClickCheck := @SyncRemoveRows;
end;

procedure InitializeWizard;
var
  I, Row: Integer;
  Ticks, Ids, Id: String;
begin
  CreateRemoveGroup;
  if not ComponentsGiven then
  begin
    Ticks := '';
    for I := 0 to PluginCount - 1 do
      if TickedByDefault(I) then
        Ticks := Ticks + ',' + PId[I]
      else
        Ticks := Ticks + ',!' + PId[I];
    WizardSelectComponents(Copy(Ticks, 2, Length(Ticks)));
  end;
  // /REMOVE together with /COMPONENTS ticks the remove rows, the way a user would.
  Ids := ExpandConstant('{param:REMOVE|}');
  while Ids <> '' do
  begin
    Id := SplitNext(Ids);
    I := FindPlugin(Id, True);
    if (RemoveList <> nil) and (I >= 0) then
      for Row := 0 to GetArrayLength(RemoveRow) - 1 do
        if RemoveRow[Row] = I then
          RemoveList.Checked[Row] := True;
  end;
  SyncRemoveRows(nil);
  for I := 0 to PluginCount - 1 do
    if IsInstalled(I) then
      WizardForm.ComponentsList.ItemCaption[I] := WizardForm.ComponentsList.ItemCaption[I] + #13#10 +
        'Found here: ' + ReadableList(PFoundFormats[I]);
  ShowStatus;
end;

function AnyInstalling: Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 0 to PluginCount - 1 do
    if IsInstalling(I) then
      Result := True;
end;

procedure CurPageChanged(CurPageID: Integer);
var
  Text: String;
begin
  if CurPageID = wpSelectComponents then
    ShowStatus
  else if CurPageID = wpFinished then
  begin
    Text := 'Setup has finished.' + #13#10#13#10;
    if AnyInstalling then
    begin
      if WizardIsTaskSelected('vst3') then
        Text := Text + 'VST3: ' + ExpandConstant('{commoncf64}\VST3') + #13#10;
      if WizardIsTaskSelected('clap') then
        Text := Text + 'CLAP: ' + ExpandConstant('{commoncf64}\CLAP') + #13#10;
      if WizardIsTaskSelected('standalone') then
        Text := Text + 'Standalone: ' + ExpandConstant('{app}') + ', and in the Start menu under {#Vendor}' + #13#10;
      Text := Text + #13#10 + 'Rescan plugins in your host.';
    end;
    WizardForm.FinishedLabel.Caption := Text;
    WizardForm.FinishedLabel.AdjustHeight;
  end;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = wpSelectTasks) and not AnyInstalling;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  I: Integer;
begin
  Result := True;
  if CurPageID = wpSelectComponents then
  begin
    SyncRemoveRows(nil);
    for I := 0 to PluginCount - 1 do
      if Result and IsInstalling(I) and (PStatus[I] = StNewer) then
        Result := SuppressibleMsgBox(PName[I] + ' ' + PFound[I] + ' is installed, newer than the ' + PVersion[I] +
          ' in this setup.' + #13#10#13#10 + 'Replace it with ' + PVersion[I] + '?', mbConfirmation, MB_YESNO, IDYES) = IDYES;
  end
  else if (CurPageID = wpSelectTasks) and AnyInstalling and not WizardIsTaskSelected('vst3')
    and not WizardIsTaskSelected('clap') and not WizardIsTaskSelected('standalone') then
  begin
    SuppressibleMsgBox('Tick at least one format.', mbError, MB_OK, IDOK);
    Result := False;
  end;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo,
  MemoGroupInfo, MemoTasksInfo: String): String;
var
  I: Integer;
  Install, Update, Remove, Formats: String;
begin
  Result := '';
  for I := 0 to PCount - 1 do
    if IsRemoving(I) then
      Remove := Remove + Space + InstalledLabel(I) + ' (' + ReadableList(PFoundFormats[I]) + ')' + NewLine
    else if IsInstalling(I) then
      case PStatus[I] of
        StNotInstalled: Install := Install + Space + PName[I] + ' ' + PVersion[I] + NewLine;
        StSame: Update := Update + Space + PName[I] + ' ' + PVersion[I] + ' (reinstall)' + NewLine;
        StNewer: Update := Update + Space + PName[I] + ' ' + PFound[I] + ' ' + #$2192 + ' ' + PVersion[I] + ' (downgrade)' + NewLine;
      else
        Update := Update + Space + InstalledLabel(I) + ' ' + #$2192 + ' ' + PVersion[I] + NewLine;
      end;
  if Install <> '' then
    Result := Result + 'Install:' + NewLine + Install + NewLine;
  if Update <> '' then
    Result := Result + 'Update:' + NewLine + Update + NewLine;
  if Remove <> '' then
    Result := Result + 'Remove:' + NewLine + Remove + NewLine;
  if AnyInstalling then
  begin
    Formats := '';
    if WizardIsTaskSelected('vst3') then
      Formats := Formats + ', VST3';
    if WizardIsTaskSelected('clap') then
      Formats := Formats + ', CLAP';
    if WizardIsTaskSelected('standalone') then
      Formats := Formats + ', Standalone';
    Result := Result + 'Formats:' + NewLine + Space + Copy(Formats, 3, Length(Formats)) + NewLine;
  end;
  if Result = '' then
    Result := 'Nothing to install or remove.';
end;

procedure RegisterExtraCloseApplicationsResources;
var
  I: Integer;
begin
  for I := 0 to PCount - 1 do
    if IsRemoving(I) then
    begin
      RegisterExtraCloseApplicationsResource(False, Vst3Binary(PName[I]));
      RegisterExtraCloseApplicationsResource(False, ClapFile(PName[I]));
      RegisterExtraCloseApplicationsResource(False, StandaloneExe(ExpandConstant('{app}'), PName[I]));
    end;
end;

// Removes the old bundle before copying, so a host with the old binary loaded keeps its mapped copy.
procedure DeleteOldFormats(I: Integer);
begin
  if InList(PFormats[I], 'VST3') and WizardIsTaskSelected('vst3') then
    DelTree(Vst3Dir(PName[I]), True, True, True);
  if InList(PFormats[I], 'CLAP') and WizardIsTaskSelected('clap') then
    DeleteFile(ClapFile(PName[I]));
  if InList(PFormats[I], 'Standalone') and WizardIsTaskSelected('standalone') then
    DeleteFile(StandaloneExe(ExpandConstant('{app}'), PName[I]));
end;

procedure WriteReceipt(I: Integer);
var
  Key, Formats, Command: String;
begin
  Formats := FormatsOnDisk(PName[I], ExpandConstant('{app}'));
  if Formats = '' then
    exit;
  Key := VendorKey + '\' + PName[I];
  RegWriteStringValue(HKLM64, Key, 'Id', PId[I]);
  RegWriteStringValue(HKLM64, Key, 'Version', PVersion[I]);
  RegWriteStringValue(HKLM64, Key, 'Formats', Formats);
  RegWriteStringValue(HKLM64, Key, 'Suite', '{#SuiteVersion}');

  Key := AppsKey + '\' + AppsPrefix + PId[I];
  // UninstallString keeps message boxes, so a removal blocked by a running host says so.
  Command := AddQuotes(ExpandConstant('{app}\') + MaintenanceExe) + ' /REMOVE=' + PId[I] + ' /VERYSILENT /NORESTART';
  RegWriteStringValue(HKLM64, Key, 'DisplayName', '{#Vendor} ' + PName[I]);
  RegWriteStringValue(HKLM64, Key, 'DisplayVersion', PVersion[I]);
  RegWriteStringValue(HKLM64, Key, 'Publisher', '{#Vendor}');
  RegWriteStringValue(HKLM64, Key, 'InstallLocation', ExpandConstant('{app}'));
  RegWriteStringValue(HKLM64, Key, 'DisplayIcon', ExpandConstant('{app}\') + MaintenanceExe);
  RegWriteStringValue(HKLM64, Key, 'UninstallString', Command);
  RegWriteStringValue(HKLM64, Key, 'QuietUninstallString', Command + ' /SUPPRESSMSGBOXES');
  RegWriteDWordValue(HKLM64, Key, 'NoModify', 1);
  RegWriteDWordValue(HKLM64, Key, 'NoRepair', 1);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  I: Integer;
begin
  if CurStep = ssInstall then
  begin
    for I := 0 to PCount - 1 do
      if IsRemoving(I) then
      begin
        if not RemovePluginFiles(PName[I], PId[I], ExpandConstant('{app}')) then
          RemoveFailed := RemoveFailed + #13#10 + PName[I];
      end
      else if IsInstalling(I) then
        DeleteOldFormats(I);
  end
  else if CurStep = ssPostInstall then
  begin
    for I := 0 to PluginCount - 1 do
      if IsInstalling(I) then
        WriteReceipt(I);
    for I := 0 to PCount - 1 do
      if not IsInstalling(I) then
        ForgetStaleReceipt(I);
    if RemoveFailed <> '' then
      SuppressibleMsgBox('These plugins could not be removed completely. Close every host that uses them and run the setup again:'
        + RemoveFailed, mbError, MB_OK, IDOK);
    UninstallSuiteAtExit := not AnyReceipt;
  end;
end;

function GetCustomSetupExitCode: Integer;
begin
  if RemoveFailed <> '' then
    Result := RemoveFailedExitCode
  else
    Result := 0;
end;

procedure DeinitializeSetup;
begin
  if UninstallSuiteAtExit then
    LaunchSuiteUninstaller;
end;

// The suite uninstaller: every plugin with a receipt, then the receipts and the per-plugin Apps entries.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Names: TArrayOfString;
  I: Integer;
  Id: String;
begin
  if CurUninstallStep <> usUninstall then
    exit;
  if RegGetSubkeyNames(HKLM64, VendorKey, Names) then
    for I := 0 to GetArrayLength(Names) - 1 do
    begin
      if not RegQueryStringValue(HKLM64, VendorKey + '\' + Names[I], 'Id', Id) or (Id = '') then
        Id := Lowercase(Names[I]);
      RemovePluginFiles(Names[I], Id, ExpandConstant('{app}'));
    end;
  RegDeleteKeyIncludingSubkeys(HKLM64, VendorKey);
  if RegGetSubkeyNames(HKLM64, AppsKey, Names) then
    for I := 0 to GetArrayLength(Names) - 1 do
      if CompareText(Copy(Names[I], 1, Length(AppsPrefix)), AppsPrefix) = 0 then
        RegDeleteKeyIncludingSubkeys(HKLM64, AppsKey + '\' + Names[I]);
end;
