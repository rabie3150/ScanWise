$binDir = "out\build\release\bin"
$portableDir = "out\ScanWise_Portable"
$zipFile = "out\ScanWise_Portable.zip"

If (Test-Path $portableDir) { Remove-Item $portableDir -Recurse -Force }
If (Test-Path $zipFile) { Remove-Item $zipFile -Force }

New-Item -ItemType Directory -Path $portableDir | Out-Null

# Copy all binaries and dependencies
Copy-Item -Path "$binDir\*" -Destination $portableDir -Recurse

# Create a friendly launcher script
$launcherPath = "$portableDir\Run_ScanWise.cmd"
$launcherContent = @"
@echo off
cd /d "%~dp0"
echo Starting ScanWise...
start "" "ScanWise.exe"
"@
Set-Content -Path $launcherPath -Value $launcherContent

# Compress to ZIP
Compress-Archive -Path "$portableDir\*" -DestinationPath $zipFile

Write-Host "Portable version created at: $zipFile"
