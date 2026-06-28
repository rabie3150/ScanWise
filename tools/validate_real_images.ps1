# Validates document detection on a set of real images.
# Usage: .\tools\validate_real_images.ps1 "C:\path\to\image1.jpg" "C:\path\to\image2.jpg"

param(
    [Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)]
    [string[]]$Images
)

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$repoRoot = Split-Path -Parent $PSScriptRoot
$appPath = Join-Path $repoRoot 'out\build\release\bin\ScanWise.exe'

Add-Type -ReferencedAssemblies System.Drawing.dll @"
using System;
using System.Runtime.InteropServices;
using System.Drawing;
using System.Drawing.Imaging;

public class Win32 {
    [DllImport("user32.dll")] public static extern bool SwitchToThisWindow(IntPtr hWnd, bool fAltTab);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);
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

    public static readonly IntPtr HWND_TOPMOST = new IntPtr(-1);
    public static readonly IntPtr HWND_NOTOPMOST = new IntPtr(-2);
    public const uint SWP_SHOWWINDOW = 0x0040;
    public const uint SWP_NOMOVE = 0x0002;
    public const uint SWP_NOSIZE = 0x0001;

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

function Focus-Window($hWnd) {
    [Win32]::ShowWindow($hWnd, 1) | Out-Null
    [Win32]::SetWindowPos($hWnd, [Win32]::HWND_TOPMOST, 0, 0, 0, 0,
        [Win32]::SWP_SHOWWINDOW -bor [Win32]::SWP_NOMOVE -bor [Win32]::SWP_NOSIZE) | Out-Null
    [Win32]::SwitchToThisWindow($hWnd, $true) | Out-Null
    [Win32]::SetForegroundWindow($hWnd) | Out-Null
    Start-Sleep -Milliseconds 200
    [Win32]::SetWindowPos($hWnd, [Win32]::HWND_NOTOPMOST, 0, 0, 0, 0,
        [Win32]::SWP_NOMOVE -bor [Win32]::SWP_NOSIZE) | Out-Null
}

function Invoke-Button($root, [string]$name) {
    $cond = [System.Windows.Automation.PropertyCondition]::new(
        [System.Windows.Automation.AutomationElement]::NameProperty, $name)
    $btn = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
    if ($btn -eq $null) { throw "Button '$name' not found" }
    $pattern = $btn.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
    $pattern.Invoke()
}

function Save-WindowShot($hWnd, [string]$path) {
    $bmp = [Win32]::CaptureWindow($hWnd)
    $bmp.Save($path)
    $bmp.Dispose()
}

$idx = 0
foreach ($img in $Images) {
    $idx++
    $proc = Start-Process -FilePath $appPath -ArgumentList "`"$img`"" -PassThru
    for ($i = 0; $i -lt 50 -and $proc.MainWindowHandle -eq 0; $i++) {
        Start-Sleep -Milliseconds 200
    }
    if ($proc.MainWindowHandle -eq 0) { throw "Window did not appear" }

    Focus-Window $proc.MainWindowHandle
    Start-Sleep -Seconds 3
    $root = [System.Windows.Automation.AutomationElement]::FromHandle($proc.MainWindowHandle)

    $shotPath = Join-Path $repoRoot "screenshot_real_${idx}_detected.png"
    Save-WindowShot $proc.MainWindowHandle $shotPath
    Write-Host "Saved $shotPath"

    Invoke-Button $root 'Apply Perspective'
    Start-Sleep -Seconds 2
    $shotPath = Join-Path $repoRoot "screenshot_real_${idx}_warped.png"
    Save-WindowShot $proc.MainWindowHandle $shotPath
    Write-Host "Saved $shotPath"

    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
}

Write-Host "Done"
