#define MyAppName "DebloatWand"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "DebloatWand"
#define MyAppExeName "DebloatWand.exe"

[Setup]
AppId={{8E5B6E9A-2CF6-4D0A-9B9E-7E0C1B3A1D4F}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64
OutputDir=.
OutputBaseFilename=DebloatWand-Setup
Compression=lzma
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#MyAppExeName}

[Files]
Source: "..\dist\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; Flags: unchecked

[Code]
const
  EnvironmentKey = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';
  PathValueName = 'Path';
  WM_SETTINGCHANGE = $001A;
  BroadcastWindow = $FFFF;
  SMTO_ABORTIFHUNG = $0002;

function SendMessageTimeout(hWnd: Integer; Msg: Cardinal; wParam: Integer;
  lParam: String; fuFlags, uTimeout: Cardinal; var lpdwResult: Integer): Integer;
  external 'SendMessageTimeoutW@user32.dll stdcall';

function NormalizePath(Value: String): String;
begin
  Result := Trim(Value);
  while (Length(Result) > 3) and (Result[Length(Result)] = '\') do
    Delete(Result, Length(Result), 1);
end;

function PathContains(PathValue, Entry: String): Boolean;
var
  CurrentEntry: String;
  SeparatorPosition: Integer;
begin
  Entry := NormalizePath(Entry);
  while Length(PathValue) > 0 do begin
    SeparatorPosition := Pos(';', PathValue);
    if SeparatorPosition = 0 then begin
      CurrentEntry := PathValue;
      PathValue := '';
    end else begin
      CurrentEntry := Copy(PathValue, 1, SeparatorPosition - 1);
      Delete(PathValue, 1, SeparatorPosition);
    end;

    if CompareText(NormalizePath(CurrentEntry), Entry) = 0 then begin
      Result := True;
      Exit;
    end;
  end;
  Result := False;
end;

procedure BroadcastEnvironmentChange;
var
  MessageResult: Integer;
begin
  SendMessageTimeout(BroadcastWindow, WM_SETTINGCHANGE, 0, 'Environment',
    SMTO_ABORTIFHUNG, 5000, MessageResult);
end;

procedure AddInstallDirToSystemPath;
var
  PathValue, InstallDir: String;
begin
  InstallDir := ExpandConstant('{app}');
  if not RegQueryStringValue(HKLM, EnvironmentKey, PathValueName, PathValue) then
    PathValue := '';

  if not PathContains(PathValue, InstallDir) then begin
    if PathValue <> '' then
      PathValue := PathValue + ';';
    PathValue := PathValue + InstallDir;
    RegWriteStringValue(HKLM, EnvironmentKey, PathValueName, PathValue);
    BroadcastEnvironmentChange;
  end;
end;

procedure RemoveInstallDirFromSystemPath;
var
  PathValue, NewPathValue, CurrentEntry, InstallDir: String;
  SeparatorPosition: Integer;
begin
  if not RegQueryStringValue(HKLM, EnvironmentKey, PathValueName, PathValue) then
    Exit;

  InstallDir := ExpandConstant('{app}');
  NewPathValue := '';
  while Length(PathValue) > 0 do begin
    SeparatorPosition := Pos(';', PathValue);
    if SeparatorPosition = 0 then begin
      CurrentEntry := PathValue;
      PathValue := '';
    end else begin
      CurrentEntry := Copy(PathValue, 1, SeparatorPosition - 1);
      Delete(PathValue, 1, SeparatorPosition);
    end;

    if CompareText(NormalizePath(CurrentEntry), NormalizePath(InstallDir)) <> 0 then begin
      if NewPathValue <> '' then
        NewPathValue := NewPathValue + ';';
      NewPathValue := NewPathValue + CurrentEntry;
    end;
  end;

  RegWriteStringValue(HKLM, EnvironmentKey, PathValueName, NewPathValue);
  BroadcastEnvironmentChange;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    AddInstallDirToSystemPath;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    RemoveInstallDirFromSystemPath;
end;
