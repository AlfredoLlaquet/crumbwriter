# Run in a disposable Windows user profile, using only the finished installers.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Setup, [Parameter(Mandatory)][string]$Baseline)
$ErrorActionPreference = 'Stop'
$Setup = (Resolve-Path $Setup).Path
$Baseline = (Resolve-Path $Baseline).Path
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\org.thecrumbs.CrumbWriter_is1'
$openWithKey = 'HKCU:\Software\Classes\.txt\OpenWithProgids'
$progIdKey = 'HKCU:\Software\Classes\CrumbWriter.Text'
$settingsDir = Join-Path $env:LOCALAPPDATA 'crumbwriter'
$startLink = Join-Path ([Environment]::GetFolderPath('Programs')) 'Crumb Writer\Crumb Writer.lnk'
$desktopLink = Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) 'Crumb Writer.lnk'
if ((Test-Path $uninstallKey) -or (Test-Path $settingsDir) -or (Test-Path $startLink) -or
    (Test-Path $desktopLink) -or (Test-Path $progIdKey)) {
    throw 'These tests require a fresh profile without a Crumb Writer installation or settings.'
}
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ("CrumbWriter-" + [guid]::NewGuid())
$installDir = Join-Path $testRoot 'Crumb Writer Núria 東京'
$work = New-Item -ItemType Directory -Path (Join-Path $testRoot 'Manuscrits')
$exe = Join-Path $installDir 'crumbwriter.exe'
$silent = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
$utf8 = [Text.UTF8Encoding]::new($false)
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
foreach ($name in 'QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QTDIR','DICPATH','CRUMBWRITER_THESAURUS_DIR') {
    Remove-Item "Env:$name" -ErrorAction SilentlyContinue
}
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_FORCE_STDERR_LOGGING = '1'

function Start-Program([string]$File, [string[]]$Arguments) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $File
    $info.WorkingDirectory = $work.FullName
    $info.UseShellExecute = $false
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    return [Diagnostics.Process]::Start($info)
}
function Run-Checked([string]$File, [string[]]$Arguments) {
    $proc = Start-Program $File $Arguments
    try {
        if (!$proc.WaitForExit(120000)) {
            $proc.Kill($true)
            throw "Timed out: $File $Arguments"
        }
        if ($proc.ExitCode -ne 0) { throw "Exit $($proc.ExitCode): $File $Arguments" }
    } finally { $proc.Dispose() }
}
function Registered([string]$Path, [string]$Name) {
    $key = Get-Item -LiteralPath $Path -ErrorAction SilentlyContinue
    if ($key) { return $key.GetValue($Name) }
}
function Assert-Preserved($files) {
    foreach ($path in $files.Keys) {
        if ((Get-FileHash -LiteralPath $path).Hash -ne $files[$path]) { throw "Modified: $path" }
    }
}
$txtDefault = Registered 'HKCU:\Software\Classes\.txt' ''
$choiceKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.txt\UserChoice'
$txtChoice = Registered $choiceKey 'ProgId'
Run-Checked $Baseline ($silent + @("/DIR=$installDir", '/TASKS=openwith'))
if ((Get-ItemProperty $uninstallKey).DisplayVersion -ne '0.0.1') { throw 'Missing baseline install' }
if (!(Test-Path -LiteralPath $startLink) -or (Test-Path -LiteralPath $desktopLink)) { throw 'Unexpected shortcut selection' }

$manuscript = Join-Path $work.FullName 'Crònica 東京.txt'
$contents = @{
    '' = "Crònica//Crumb 1: Café: The author’s manuscript.`n"
    '.bookmarks' = '{"version":2,"lastCategory":"","counters":{},"bookmarks":[]}'
    '.characters' = 'Núria, the writer'
    '.recurring' = 'Cafè, coffee'
    '.dictionary' = 'Crònica'
    '.config' = 'disable-general-title=true'
    '.bak' = 'An existing backup'
}
$preserved = @{}
foreach ($suffix in $contents.Keys) {
    $file = $manuscript + $suffix
    [IO.File]::WriteAllText($file, $contents[$suffix], $utf8)
    $preserved[$file] = (Get-FileHash -LiteralPath $file).Hash
}
# This profile and an unrelated file in the install directory both belong to the writer.
$null = New-Item -Force -ItemType Directory -Path $settingsDir
$settings = Join-Path $settingsDir 'settings.ini'
[IO.File]::WriteAllText($settings, "[editor]`r`nsave-delay=3`r`n", $utf8)
$preserved[$settings] = (Get-FileHash -LiteralPath $settings).Hash
$ownFile = Join-Path $installDir "An author’s working notes.txt"
[IO.File]::WriteAllText($ownFile, 'A private note.', $utf8)
$preserved[$ownFile] = (Get-FileHash -LiteralPath $ownFile).Hash

Run-Checked $Setup ($silent + @('/TASKS=desktopicon,openwith'))
if ((Get-ItemProperty $uninstallKey).DisplayVersion -ne (Get-Item $exe).VersionInfo.ProductVersion) { throw 'Upgrade did not replace the registered version' }
if (!(Test-Path $desktopLink) -or !(Test-Path $startLink)) { throw 'Upgrade did not install selected shortcuts' }
if (!(Get-Item $openWithKey).GetValueNames().Contains('CrumbWriter.Text')) { throw 'Missing Open with registration' }
if ((Registered $choiceKey 'ProgId') -ne $txtChoice -or (Registered 'HKCU:\Software\Classes\.txt' '') -ne $txtDefault) { throw 'Default text editor was changed' }
foreach ($item in 'LICENSE','CrumbWriter.txt','dictionaries/en_US.aff','dictionaries/en_US.dic','thesaurus/th_en_US_new.dat','thesaurus/th_en_US_new.idx','third-party/MyThes/WordNet_license.txt','third-party/InnoSetup/LICENSE.txt','plugins/platforms/qwindows.dll','qt.conf') {
    if (!(Test-Path -LiteralPath (Join-Path $installDir $item))) { throw "Missing installed component: $item" }
}
Assert-Preserved $preserved
Run-Checked $exe @('--smoke-test')
$gui = Start-Program $exe @('--', $manuscript)
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(45)
    do {
        Start-Sleep -Milliseconds 200
        $gui.Refresh()
        if ($gui.HasExited) { throw "Editor exited before opening the manuscript: $($gui.ExitCode)" }
    } while ($gui.MainWindowTitle -notlike 'Crònica 東京.txt*' -and [DateTime]::UtcNow -lt $deadline)
    if ($gui.MainWindowTitle -notlike 'Crònica 東京.txt*') { throw "Unexpected manuscript window: $($gui.MainWindowTitle)" }
    Run-Checked $exe @('--', $manuscript)
    if (!$gui.CloseMainWindow() -or !$gui.WaitForExit(20000) -or $gui.ExitCode -ne 0) { throw 'Editor did not close normally' }
} finally {
    if (!$gui.HasExited) { $gui.Kill($true) }
    $gui.Dispose()
}
# The editor may legitimately record the last document while open.
$preserved[$settings] = (Get-FileHash -LiteralPath $settings).Hash
Assert-Preserved $preserved
$uninstaller = (Get-ChildItem -LiteralPath $installDir -Filter 'unins*.exe' -File).FullName
Run-Checked $uninstaller $silent
if ((Test-Path $uninstallKey) -or (Test-Path $exe) -or (Test-Path $desktopLink) -or (Test-Path $startLink) -or (Test-Path $progIdKey)) { throw 'Uninstall left program files, a registration, or shortcuts' }
if (($null -ne (Registered $openWithKey 'CrumbWriter.Text')) -or
    (Registered $choiceKey 'ProgId') -ne $txtChoice -or (Registered 'HKCU:\Software\Classes\.txt' '') -ne $txtDefault) { throw 'Uninstall damaged file associations' }
Assert-Preserved $preserved
Write-Output 'Installed, upgraded, launched, reopened, and uninstalled successfully. Writer-owned files and settings survived.'
