; Script generated by the Inno Setup Script Wizard.
; SEE THE DOCUMENTATION FOR DETAILS ON CREATING INNO SETUP SCRIPT FILES!

[Setup]
AppName=ws3270
AppVerName=ws3270 3.3.9a2
AppPublisher=Paul Mattes
AppPublisherURL=http://x3270.bgp.nu
AppSupportURL=http://x3270.bgp.nu
AppUpdatesURL=http://x3270.bgp.nu
AppCopyright=Copyright (C) 1989-2008 by Paul Mattes, GTRC and others
WizardSmallImageFile=ws3270.bmp
DefaultDirName={pf}\ws3270
DisableDirPage=no
DefaultGroupName=ws3270
AllowNoIcons=yes
OutputBaseFilename=ws3270-3.3.9a2-setup
OutputDir=\\Melville\pdm\psrc\x3270\Release\Inno\ws3270
Compression=lzma
SolidCompression=yes
ChangesAssociations=yes
MinVersion=4.0,5.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "\\Melville\pdm\psrc\x3270\Release\Inno\ws3270\ws3270.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "\\Melville\pdm\psrc\x3270\Release\Inno\ws3270\w3n4.dll"; DestDir: "{app}"; Flags: ignoreversion; OnlyBelowVersion: 0.0,5.01
Source: "\\Melville\pdm\psrc\x3270\Release\Inno\ws3270\w3n46.dll"; DestDir: "{app}"; Flags: ignoreversion; MinVersion: 0.0,5.01
Source: "\\Melville\pdm\psrc\x3270\Release\Inno\ws3270\shf.dll"; DestDir: "{app}"; Flags: ignoreversion; MinVersion: 0.0,5.0
Source: "\\Melville\pdm\psrc\x3270\Source\3.3svn\ws3270\html\Bugs.html"; DestDir: "{app}\html"; Flags: ignoreversion
Source: "\\Melville\pdm\psrc\x3270\Source\3.3svn\ws3270\html\Build.html"; DestDir: "{app}\html"; Flags: ignoreversion
Source: "\\Melville\pdm\psrc\x3270\Source\3.3svn\ws3270\html\FAQ.html"; DestDir: "{app}\html"; Flags: ignoreversion
Source: "\\Melville\pdm\psrc\x3270\Source\3.3svn\ws3270\html\Intro.html"; DestDir: "{app}\html"; Flags: ignoreversion
Source: "\\Melville\pdm\psrc\x3270\Source\3.3svn\ws3270\html\Lineage.html"; DestDir: "{app}\html"; Flags: ignoreversion
Source: "\\Melville\pdm\psrc\x3270\Source\3.3svn\ws3270\html\ReleaseNotes.html"; DestDir: "{app}\html"; Flags: ignoreversion
Source: "\\Melville\pdm\psrc\x3270\Source\3.3svn\ws3270\html\README.html"; DestDir: "{app}\html"; Flags: ignoreversion
Source: "\\Melville\pdm\psrc\x3270\Source\3.3svn\ws3270\html\ws3270-man.html"; DestDir: "{app}\html"; Flags: ignoreversion
Source: "\\Melville\pdm\psrc\x3270\Source\3.3svn\ws3270\html\Wishlist.html"; DestDir: "{app}\html"; Flags: ignoreversion
Source: "\\Melville\pdm\psrc\x3270\Source\3.3svn\ws3270\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "\\Melville\pdm\psrc\x3270\Source\3.3svn\ws3270\README.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "\\Melville\pdm\psrc\x3270\Source\3.3svn\ws3270\x3270_glue.expect"; DestDir: "{app}"; Flags: ignoreversion
; NOTE: Don't use "Flags: ignoreversion" on any shared system files

[Dirs]
Name: "{userappdata}\ws3270";

[Code]
function myHelp(Param: String): String;
begin
 result := '/c start ' + GetShortName(ExpandConstant('{app}') + '\html\README.html');
end;

[Icons]
Name: "{group}\Run ws3270"; Filename: "{app}\ws3270.exe"; WorkingDir: "{app}"
Name: "{group}\ws3270 Documentation"; Filename: "{app}\html\README.html"

[Run]
Filename: "{cmd}"; Parameters: {code:MyHelp}; Description: "{cm:LaunchProgram,Online Documentation}"; Flags: nowait postinstall skipifsilent
