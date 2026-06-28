# Installs the Windows App SDK 1.6 runtime packages required by ScanWise.
# Run this once per machine as Administrator.
# The MSIX files are expected either next to this script (after a build) or under deps/nuget.

$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# Try the MSIX files copied next to the executable first.
$msixDir = $scriptDir
if (-not (Get-ChildItem -Path $msixDir -Filter '*.msix' -ErrorAction SilentlyContinue)) {
    # Fall back to the NuGet package directory (developer / repo-root scenario).
    # Walk up from the script location to find deps/nuget.
    $repoRoot = $scriptDir
    $wasdk = $null
    for ($i = 0; $i -lt 5; $i++) {
        $candidate = Join-Path $repoRoot 'deps\nuget'
        $wasdk = Get-ChildItem -Path $candidate -Filter 'Microsoft.WindowsAppSDK.*' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
        if ($wasdk) { break }
        $parent = Split-Path -Parent $repoRoot
        if ($parent -eq $repoRoot) { break }
        $repoRoot = $parent
    }
    if (-not $wasdk) {
        Write-Error "Windows App SDK NuGet package not found. Run: deps/nuget.exe install Microsoft.WindowsAppSDK -Version 1.6.250602001 -OutputDirectory deps/nuget"
    }
    $msixDir = "$wasdk\tools\MSIX\win10-x64"
}

if (-not (Test-Path $msixDir)) {
    Write-Error "MSIX runtime packages not found in $msixDir"
}

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
