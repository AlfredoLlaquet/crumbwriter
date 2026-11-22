# Compile the staged per-user installer with the Inno Setup 6 compiler on PATH.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDir,
    [switch]$WithUpgradeFixture
)
$ErrorActionPreference = 'Stop'
$build = (Resolve-Path $BuildDir).Path
$payload = Join-Path $build 'stage'
$version = (Get-Item (Join-Path $payload 'crumbwriter.exe')).VersionInfo.ProductVersion
if ($version -notmatch '^\d+\.\d+(\.\d+){0,2}$') {
    throw "Invalid Crumb Writer version resource: '$version'"
}
$iscc = Get-Command ISCC.exe -ErrorAction SilentlyContinue
if ($iscc) { $compiler = $iscc.Source }
else { $compiler = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe' }
if (!(Test-Path -LiteralPath $compiler)) { throw 'Inno Setup 6 (ISCC.exe) is required.' }
function Compile-Installer([string]$Version, [string]$Name) {
    & $compiler "/DAppVersion=$Version" "/DPayloadDir=$payload" "/DOutputFolder=$build" "/DInstallerName=$Name" (Join-Path $PSScriptRoot 'setup.iss')
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed with exit code $LASTEXITCODE." }
}
Compile-Installer $version 'CrumbWriter-Windows-x64-Setup'
if ($WithUpgradeFixture) {
    # Same AppId and real payload; only the installed version is older.
    Compile-Installer '0.0.1' 'CrumbWriter-UpgradeFixture'
}
