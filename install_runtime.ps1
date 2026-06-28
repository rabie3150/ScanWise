# Installs the Windows App SDK 1.6 runtime packages required by ScanWise.
# Run this once per machine (or distribute the MSIX files alongside the .exe).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$wasdk = Get-ChildItem -Path "$root\deps\nuget" -Filter 'Microsoft.WindowsAppSDK.*' | Select-Object -First 1 -ExpandProperty FullName
if (-not $wasdk) {
    Write-Error "Windows App SDK NuGet package not found. Run: deps/nuget.exe install Microsoft.WindowsAppSDK -OutputDirectory deps/nuget"
}
$msixDir = "$wasdk\tools\MSIX\win10-x64"
$packages = @(
    'Microsoft.WindowsAppRuntime.Main.1.6.msix',
    'Microsoft.WindowsAppRuntime.Singleton.1.6.msix',
    'Microsoft.WindowsAppRuntime.DDLM.1.6.msix',
    'Microsoft.WindowsAppRuntime.1.6.msix'
)
foreach ($p in $packages) {
    $path = Join-Path $msixDir $p
    if (-not (Test-Path $path)) { continue }
    Write-Host "Installing $p ..."
    try {
        Add-AppxPackage -Path $path -ErrorAction Stop
        Write-Host "  OK"
    } catch {
        if ($_ -match 'higher version.*already installed') {
            Write-Host "  Already present / newer"
        } else {
            Write-Host "  FAILED: $_"
        }
    }
}
Write-Host "Done."
