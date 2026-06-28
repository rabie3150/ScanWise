# Builds the Inno Setup installer for ScanWise.
# Requires iscc.exe (Inno Setup compiler) to be available.

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$issPath = Join-Path $repoRoot 'tools\ScanWise.iss'

# Locate Inno Setup compiler
$iscc = Get-Command iscc.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
if (-not $iscc) {
    $defaultPath = "${env:ProgramFiles(x86)}\Inno Setup 6\iscc.exe"
    if (Test-Path $defaultPath) { $iscc = $defaultPath }
}
if (-not $iscc) {
    $defaultPath = "${env:ProgramFiles}\Inno Setup 6\iscc.exe"
    if (Test-Path $defaultPath) { $iscc = $defaultPath }
}
if (-not $iscc) {
    throw "iscc.exe not found. Install Inno Setup 6 from https://jrsoftware.org/isdl.php"
}

Write-Host "Using Inno Setup compiler: $iscc"
& $iscc $issPath
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compiler failed with exit code $LASTEXITCODE" }

$setupPath = Join-Path $repoRoot 'out\ScanWise_Setup.exe'
if (-not (Test-Path $setupPath)) { throw "Installer was not created at $setupPath" }
Write-Host "Created $setupPath"
