# Exercise the distributed x64 Qt runtime on the Windows client desktop.
param([Parameter(Mandatory)][string]$Setup, [Parameter(Mandatory)][string]$Driver)
$ErrorActionPreference = 'Stop'
$output = Join-Path $env:RUNNER_TEMP 'gui-audit'
$install = Join-Path $env:RUNNER_TEMP 'CrumbWriter-client'
$null = New-Item -ItemType Directory -Force -Path $output
$os = Get-CimInstance Win32_OperatingSystem
$edition = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
$hostDetails = [ordered]@{
    caption = $os.Caption
    productName = $edition.ProductName
    edition = $edition.EditionID
    displayVersion = $edition.DisplayVersion
    productType = [int]$os.ProductType
    version = $os.Version
    build = [int]$os.BuildNumber
    architecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
}
$description = $hostDetails | ConvertTo-Json
$description | Set-Content -LiteralPath (Join-Path $output 'windows-host.json') -Encoding utf8
Write-Output $description
if ($hostDetails.productType -ne 1 -or $hostDetails.build -lt 22000 -or
    $hostDetails.caption -notmatch 'Windows 11' -or $hostDetails.architecture -ne 'Arm64') {
    throw 'The appearance audit requires a Windows 11 Arm client desktop.'
}

function Invoke-Checked([string]$File, [string[]]$Arguments) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = (Resolve-Path -LiteralPath $File).Path
    $info.UseShellExecute = $false
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($info)
    try {
        if (!$process.WaitForExit(360000)) {
            $process.Kill($true)
            throw "Timed out running $File"
        }
        if ($process.ExitCode -ne 0) { throw "$File returned $($process.ExitCode)" }
    } finally { $process.Dispose() }
}

$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
foreach ($name in 'QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QTDIR','DICPATH') {
    Remove-Item "Env:$name" -ErrorAction SilentlyContinue
}
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_FORCE_STDERR_LOGGING = '1'
Invoke-Checked $Setup @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOICONS', "/DIR=$install")
Invoke-Checked (Join-Path $install 'crumbwriter.exe') @('--smoke-test')
$env:DICPATH = Join-Path $install 'dictionaries'
$installedDriver = Join-Path $install 'test-gui-audit.exe'
Copy-Item -LiteralPath $Driver -Destination $installedDriver
Invoke-Checked $installedDriver @('--output', $output, '--platform-name', 'windows')
