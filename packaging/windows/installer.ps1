# Compile the staged per-user installer with the Inno Setup 6 compiler on PATH.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDir,
    [switch]$WithUpgradeFixture,
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
$build = (Resolve-Path $BuildDir).Path
$payload = Join-Path $build 'stage'
$version = (Get-Item (Join-Path $payload 'crumbwriter.exe')).VersionInfo.ProductVersion
if ($version -notmatch '^\d+\.\d+(\.\d+){0,2}$') {
    throw "Invalid Crumb Writer version resource: '$version'"
}
$compiler = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
if (!(Test-Path -LiteralPath $compiler)) { $compiler = (Get-Command ISCC.exe -ErrorAction Stop).Source }
Write-Output ("Installer compiler: " + $compiler + " " + (Get-Item $compiler).VersionInfo.ProductVersion)
if (!$CheckOnly) {
    $notice = Join-Path (Split-Path $compiler) 'license.txt'
    if (!(Test-Path -LiteralPath $notice)) { throw "Missing Inno Setup license beside $compiler" }
    $target = New-Item -ItemType Directory -Force -Path (Join-Path $payload 'third-party/InnoSetup')
    Copy-Item -LiteralPath $notice -Destination (Join-Path $target.FullName 'LICENSE.txt')
}
function Compile-Installer([string]$Version, [string]$Name) {
    $options = @()
    if ($CheckOnly) { $options += '/O-' }
    & $compiler @options "/DAppVersion=$Version" "/DPayloadDir=$payload" "/DOutputFolder=$build" "/DInstallerName=$Name" (Join-Path $PSScriptRoot 'setup.iss')
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed with exit code $LASTEXITCODE." }
}
Compile-Installer $version 'CrumbWriter-Windows-x64-Setup'
if ($WithUpgradeFixture) {
    # Same AppId and real payload; only the installed version is older.
    Compile-Installer '0.0.1' 'CrumbWriter-UpgradeFixture'
}
