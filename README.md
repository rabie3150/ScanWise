# ScanWise

[![CI](https://github.com/rabie3150/ScanWise/actions/workflows/ci.yml/badge.svg)](https://github.com/rabie3150/ScanWise/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Release](https://img.shields.io/github/v/release/rabie3150/ScanWise)](https://github.com/rabie3150/ScanWise/releases/latest)

Turn photos of documents into clean, straight, shareable scans — on Windows, entirely offline, and free.

[⬇ Download ScanWise for Windows](https://github.com/rabie3150/ScanWise/releases/latest) (Windows 10 version 1809+ / Windows 11 x64)

![ScanWise screenshot](docs/app_screenshot.png)

## Features

- **Drag & Drop / Clipboard Import** — drop image files onto the window or paste from the clipboard.
- **Auto Straighten** — automatic document edge detection with draggable corner handles for manual correction.
- **Perspective Flattening** — four-point transform to straighten angled or tilted shots.
- **Filter Presets** — Original, Black & White, Document, Magic Color, and Photo.
- **Manual Adjustments** — brightness, contrast, and saturation sliders.
- **Multi-Page Support** — add, remove, and reorder pages in the sidebar.
- **Export** — single-page PNG/JPEG or multi-page PDF.

## Get Started in 3 Steps

1. Download and run `ScanWise_Setup.exe` from the [latest release](https://github.com/rabie3150/ScanWise/releases/latest).
2. Drag a document photo onto the window (or paste with `Ctrl+V`).
3. Click **Magic Perspective**, choose a filter, and export as PNG, JPEG, or PDF.

> **Runtime note:** ScanWise is an unpackaged WinUI 3 app. The installer can install the Windows App SDK runtime automatically when run as Administrator. If you use the portable ZIP instead, run `install_runtime.ps1` as Administrator once per machine.

## Download Options

| Asset | Description |
|-------|-------------|
| `ScanWise_Setup.exe` | Windows installer with Start Menu shortcuts |
| `ScanWise_Portable.zip` | Portable version, no installation needed |

## Run Tests

```powershell
.\out\build\release\bin\scanwise_engine_tests.exe
```

## Build from Source

### Requirements

- Windows 10 version 1809+ / Windows 11 x64
- Visual Studio 2022 (Community, Professional, Enterprise, or Build Tools) with:
  - **Desktop development with C++** workload
  - **C++ CMake tools for Windows** component (includes Ninja)
  - Windows 10/11 SDK
- CMake 3.28+
- PowerShell 5.1+
- Inno Setup 6 (only if building the installer)

> If PowerShell blocks local scripts, run `Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser` first, or invoke scripts with `-ExecutionPolicy Bypass`.

### Quick Build

> **Note:** The first build compiles OpenCV from source, which can take 15–30 minutes depending on your machine.

Open a PowerShell window and run:

```powershell
# 1. Clone dependency sources (one-time)
git clone --branch 4.11.0 --depth 1 https://github.com/opencv/opencv.git deps/opencv
git clone --branch v2.4.4 --depth 1 https://github.com/libharu/libharu.git deps/libharu

# 2. Fetch WinUI 3 NuGet packages (one-time)
#    Download nuget.exe from https://dist.nuget.org/win-x86-commandline/latest/nuget.exe into deps/nuget.exe, then:
deps/nuget.exe install Microsoft.WindowsAppSDK -Version 1.6.250602001 -OutputDirectory deps/nuget
deps/nuget.exe install Microsoft.Windows.CppWinRT -Version 3.0.260520.1 -OutputDirectory deps/nuget
deps/nuget.exe install Microsoft.Web.WebView2 -Version 1.0.2651.64 -OutputDirectory deps/nuget

# 3. Configure & build
.\configure_release.ps1
.\build_release.ps1
```

Output binaries:

```
out\build\release\bin\ScanWise.exe
out\build\release\bin\scanwise_engine_tests.exe
```

### Install the Windows App SDK Runtime

After building, run as Administrator:

```powershell
.\out\build\release\bin\install_runtime.ps1
```

Then launch the app:

```powershell
.\out\build\release\bin\ScanWise.exe
```

You can also open an image directly on startup:

```powershell
.\out\build\release\bin\ScanWise.exe "C:\path\to\image.jpg"
```

### Build the Installer Locally

```powershell
.\tools\build_installer.ps1
```

This produces `out\ScanWise_Setup.exe`.

## Project Layout

- `engine/` — OS-agnostic image processing core (OpenCV static).
- `app/winui/` — Windows UI shell (WinUI 3 / C++/WinRT).
- `deps/` — OpenCV, libharu, and WinUI 3 NuGet packages (fetched at build time, not committed).
- `tools/` — Developer utilities, packaging scripts, and UI automation helpers.
- `assets/` — App icons and store assets.

## Built With

- C++20
- WinUI 3 / C++/WinRT
- OpenCV 4.11.0
- libharu 2.4.4
- CMake + Ninja

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](LICENSE)
