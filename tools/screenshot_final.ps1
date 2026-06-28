# Captures a clean screenshot.png of the empty ScanWise app window.

Add-Type -AssemblyName System.Drawing

$repoRoot = Split-Path -Parent $PSScriptRoot
$appPath = Join-Path $repoRoot 'out\build\release\bin\ScanWise.exe'

Add-Type -ReferencedAssemblies System.Drawing.dll @"
using System;
using System.Runtime.InteropServices;
using System.Drawing;

public class Win32 {
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);
    [DllImport("user32.dll")] public static extern IntPtr GetWindowDC(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);
    [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleDC(IntPtr hdc);
    [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleBitmap(IntPtr hdc, int nWidth, int nHeight);
    [DllImport("gdi32.dll")] public static extern IntPtr SelectObject(IntPtr hdc, IntPtr hgdiobj);
    [DllImport("gdi32.dll")] public static extern bool DeleteObject(IntPtr hObject);
    [DllImport("gdi32.dll")] public static extern bool DeleteDC(IntPtr hdc);

    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }

    public static Bitmap CaptureWindow(IntPtr hWnd) {
        RECT rc;
        GetWindowRect(hWnd, out rc);
        int w = rc.Right - rc.Left;
        int h = rc.Bottom - rc.Top;
        IntPtr hdcSrc = GetWindowDC(hWnd);
        IntPtr hdcDest = CreateCompatibleDC(hdcSrc);
        IntPtr hBitmap = CreateCompatibleBitmap(hdcSrc, w, h);
        IntPtr hOld = SelectObject(hdcDest, hBitmap);
        PrintWindow(hWnd, hdcDest, 0x00000002);
        Bitmap bmp = Bitmap.FromHbitmap(hBitmap);
        SelectObject(hdcDest, hOld);
        DeleteObject(hBitmap);
        DeleteDC(hdcDest);
        ReleaseDC(hWnd, hdcSrc);
        return bmp;
    }
}
"@

$proc = Start-Process -FilePath $appPath -PassThru
for ($i = 0; $i -lt 50 -and $proc.MainWindowHandle -eq 0; $i++) { Start-Sleep -Milliseconds 200 }
if ($proc.MainWindowHandle -eq 0) { throw "Window did not appear" }
[Win32]::ShowWindow($proc.MainWindowHandle, 1) | Out-Null
[Win32]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
Start-Sleep -Seconds 2
$bmp = [Win32]::CaptureWindow($proc.MainWindowHandle)
$bmp.Save((Join-Path $repoRoot 'screenshot.png'))
$bmp.Dispose()
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Write-Host "Saved screenshot.png"
