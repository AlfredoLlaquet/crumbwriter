; ISCC /DAppVersion=2.4 /DPayloadDir=... /DOutputFolder=... setup.iss
#ifndef AppVersion
  #error AppVersion is required
#endif
#ifndef PayloadDir
  #error PayloadDir is required
#endif
#ifndef OutputFolder
  #error OutputFolder is required
#endif
#ifndef InstallerName
  #define InstallerName "CrumbWriter-Windows-x64-Setup"
#endif

[Setup]
AppId=org.thecrumbs.CrumbWriter
AppName=Crumb Writer
AppVersion={#AppVersion}
AppPublisher=Alfredo Llaquet-Alsina
AppPublisherURL=https://github.com/AlfredoLlaquet/crumbwriter
AppSupportURL=https://github.com/AlfredoLlaquet/crumbwriter/issues
AppUpdatesURL=https://github.com/AlfredoLlaquet/crumbwriter/releases
DefaultDirName={localappdata}\Programs\Crumb Writer
DefaultGroupName=Crumb Writer
DisableProgramGroupPage=yes
AllowNoIcons=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362
AppMutex=Local\org.thecrumbs.CrumbWriter.Running
CloseApplications=no
RestartApplications=no
ChangesAssociations=yes
WizardStyle=modern
DisableWelcomePage=no
UninstallDisplayIcon={app}\crumbwriter.exe
SetupIconFile=crumbwriter.ico
OutputDir={#OutputFolder}
OutputBaseFilename={#InstallerName}
Compression=lzma2
SolidCompression=yes
VersionInfoVersion={#AppVersion}
VersionInfoDescription=Crumb Writer Setup

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked
Name: "openwith"; Description: "List Crumb Writer in &Open with for text files"; GroupDescription: "File Explorer:"

[Files]
Source: "{#PayloadDir}\LICENSE"; DestName: "CrumbWriter-License.txt"; Flags: dontcopy
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{group}\Crumb Writer"; Filename: "{app}\crumbwriter.exe"
Name: "{userdesktop}\Crumb Writer"; Filename: "{app}\crumbwriter.exe"; Tasks: desktopicon

[InstallDelete]
Type: files; Name: "{userdesktop}\Crumb Writer.lnk"; Check: not WizardIsTaskSelected('desktopicon') and WasDesktopShortcutSelected

[Registry]
; The default handler and UserChoice belong to Windows and the writer.
Root: HKCU; Subkey: "Software\Classes\CrumbWriter.Text"; ValueType: string; ValueData: "Text manuscript"; Flags: uninsdeletekey; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\CrumbWriter.Text\DefaultIcon"; ValueType: string; ValueData: """{app}\crumbwriter.exe"",0"; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\CrumbWriter.Text\shell\open\command"; ValueType: string; ValueData: """{app}\crumbwriter.exe"" -- ""%1"""; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.txt\OpenWithProgids"; ValueType: string; ValueName: "CrumbWriter.Text"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.txt\OpenWithProgids"; ValueName: "CrumbWriter.Text"; Flags: deletevalue; Check: not WizardIsTaskSelected('openwith')
Root: HKCU; Subkey: "Software\Classes\CrumbWriter.Text"; Flags: deletekey; Check: not WizardIsTaskSelected('openwith')

[Run]
Filename: "{app}\crumbwriter.exe"; Description: "Launch &Crumb Writer"; Flags: nowait postinstall skipifsilent

[Code]
function WasDesktopShortcutSelected: Boolean;
begin
  Result := GetPreviousData('DesktopShortcut', '0') = '1';
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  if WizardIsTaskSelected('desktopicon') then
    SetPreviousData(PreviousDataKey, 'DesktopShortcut', '1')
  else
    SetPreviousData(PreviousDataKey, 'DesktopShortcut', '0');
end;

procedure ViewLicense(Sender: TObject);
var
  Text: AnsiString;
begin
  ExtractTemporaryFile('CrumbWriter-License.txt');
  if LoadStringFromFile(ExpandConstant('{tmp}\CrumbWriter-License.txt'), Text) then
    MsgBox(String(Text), mbInformation, MB_OK);
end;

procedure InitializeWizard;
var
  LicenseButton: TNewButton;
begin
  LicenseButton := TNewButton.Create(WizardForm);
  LicenseButton.Parent := WizardForm;
  LicenseButton.Caption := 'View &License';
  LicenseButton.SetBounds(ScaleX(12), WizardForm.NextButton.Top,
                          ScaleX(96), WizardForm.NextButton.Height);
  LicenseButton.Anchors := [akLeft, akBottom];
  LicenseButton.OnClick := @ViewLicense;
end;
