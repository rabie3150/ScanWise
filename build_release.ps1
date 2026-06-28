# Builds the ScanWise release configuration.
# Auto-detects any Visual Studio 2022 installation (Community, Professional, Enterprise, or Build Tools).

$ErrorActionPreference = 'Stop'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found. Install Visual Studio 2022 with the Desktop development with C++ workload."
}

$vsInstallPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstallPath) {
    throw "Visual Studio 2022 with C++ tools not found."
}

$devShellDll = Join-Path $vsInstallPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
if (-not (Test-Path $devShellDll)) {
    throw "Visual Studio developer shell not found at $devShellDll."
}

Import-Module $devShellDll
Enter-VsDevShell -VsInstallPath $vsInstallPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64'

cmake --build --preset release
