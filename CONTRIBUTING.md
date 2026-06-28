# Contributing to ScanWise

Thank you for your interest in ScanWise. This project is a native Windows document scanner built with C++20, WinUI 3, and OpenCV.

## Getting started

1. Fork the repository.
2. Clone your fork.
3. Follow the build instructions in [`README.md`](README.md).
4. Make your changes on a feature branch.
5. Ensure the engine tests pass:
   ```powershell
   .\out\build\release\bin\scanwise_engine_tests.exe
   ```
6. Open a pull request against `main`.

## Build requirements

- Windows 10/11 x64
- Visual Studio 2022 (Community, Professional, Enterprise, or Build Tools) with the **Desktop development with C++** workload
- CMake 3.28+
- PowerShell

## What to contribute

- Bug fixes for document detection, perspective warp, filters, or export.
- UI improvements that keep the app lightweight and native.
- Engine tests for new detection scenarios.
- Documentation and README improvements.

## Code style

- Keep engine code dependency-free except for OpenCV and libharu.
- Prefer minimal, focused changes.
- The WinUI app should not pull in `opencv_imgcodecs`; image I/O goes through `wic_helpers.cpp`.

## Questions?

Open an issue and we will help.
