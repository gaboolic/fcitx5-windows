; Fcitx5 TSF IME — Inno Setup 6 wizard (x64 only).
; Build: install Inno Setup 6, then run build-installer.ps1 -StageDir <stage root>
; AppId / IME CLSID: keep stable across releases (ARP upgrade path).

#define MyAppName "Fcitx5 (TSF IME)"
#define MyAppPublisher "Fcitx5 Windows"
#define MyAppURL "https://github.com/fcitx/fcitx5"
#define MyAppId "{{A3D6F0C8-7B2E-4F91-9C1D-8E4A5B6C7D8E}"
#define ImeClsid "{{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}}"

#ifndef StageDir
  #define StageDir "..\stage-pinyin"
#endif

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion=1.0.0
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf64}\Fcitx5
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
WizardStyle=modern
OutputDir=dist
OutputBaseFilename=Fcitx5TSF-Setup
Compression=lzma2/max
SolidCompression=yes
WizardSizePercent=110,100
DisableWelcomePage=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopuninstall"; Description: "Create desktop shortcut to uninstall"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: ".git\*"

[Icons]
Name: "{autoprograms}\{#MyAppName}\Settings"; Filename: "{app}\bin\fcitx5-config-win32.exe"
Name: "{autoprograms}\{#MyAppName}\Uninstall"; Filename: "{uninstallexe}"
Name: "{autoprograms}\{#MyAppName}\Fcitx5 user config"; Filename: "{win}\explorer.exe"; Parameters: """{userappdata}\Fcitx5"""
Name: "{commondesktop}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"; Tasks: desktopuninstall

[UninstallRun]
Filename: "{sys}\regsvr32.exe"; \
  Parameters: "/u /s ""{code:GetImeDll}"""; \
  WorkingDir: "{app}\bin"; \
  RunOnceId: "Fcitx5TsfUnreg"; \
  Flags: runhidden waituntilterminated; \
  Check: UninstallImeDllExists

[Code]

function UninstallImeDllExists: Boolean;
begin
  Result := (GetImeDll('') <> '');
end;

function GetImeDll(Param: string): string;
begin
  if FileExists(ExpandConstant('{app}\bin\libfcitx5-x86_64.dll')) then
    Result := ExpandConstant('{app}\bin\libfcitx5-x86_64.dll')
  else if FileExists(ExpandConstant('{app}\bin\fcitx5-x86_64.dll')) then
    Result := ExpandConstant('{app}\bin\fcitx5-x86_64.dll')
  else
    Result := '';
end;

procedure RegPurgeFcitxResiduals;
var
  ClsidKey: String;
  TipKey: String;
begin
  ClsidKey := 'CLSID\{#ImeClsid}';
  TipKey := 'SOFTWARE\Microsoft\CTF\TIP\{#ImeClsid}';
  if RegKeyExists(HKEY_CLASSES_ROOT, ClsidKey) then
    RegDeleteKeyIncludingSubkeys(HKEY_CLASSES_ROOT, ClsidKey);
  if RegKeyExists(HKEY_LOCAL_MACHINE, TipKey) then
    RegDeleteKeyIncludingSubkeys(HKEY_LOCAL_MACHINE, TipKey);
  if RegKeyExists(HKEY_CURRENT_USER, 'Software\Microsoft\CTF\TIP\{#ImeClsid}') then
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Microsoft\CTF\TIP\{#ImeClsid}');
  if RegKeyExists(HKEY_CURRENT_USER, 'Software\Classes\CLSID\{#ImeClsid}') then
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Classes\CLSID\{#ImeClsid}');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Dll: String;
  R: Integer;
begin
  if CurStep = ssPostInstall then
  begin
    Dll := GetImeDll('');
    if Dll = '' then
      RaiseException('IME DLL not found under ' + ExpandConstant('{app}\bin') + '. Rebuild the stage and installer (see installer/README.txt).');
    if (not Exec(ExpandConstant('{sys}\regsvr32.exe'), '/s "' + Dll + '"', ExtractFileDir(Dll), SW_HIDE, ewWaitUntilTerminated, R)) or (R <> 0) then
      RaiseException('regsvr32 failed (code ' + IntToStr(R) + '). Run the installer as Administrator.');
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RegPurgeFcitxResiduals;
end;
