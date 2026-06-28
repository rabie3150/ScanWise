# Generates a synthetic test document image for UI automation and manual testing.
# Output: test_scan.png in the repository root.

Add-Type -AssemblyName System.Drawing

$repoRoot = Split-Path -Parent $PSScriptRoot
$outputPath = Join-Path $repoRoot 'test_scan.png'

$bmp = New-Object System.Drawing.Bitmap 640, 480
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromArgb(80, 80, 80))

# Draw a white document-like rectangle with perspective-ish offset
$g.FillRectangle([System.Drawing.Brushes]::White, 100, 60, 440, 360)
$g.DrawRectangle([System.Drawing.Pens]::Black, 100, 60, 439, 359)

$font = New-Object System.Drawing.Font('Arial', 28)
$g.DrawString('ScanWise Test Document', $font, [System.Drawing.Brushes]::Black, 130, 140)

$font2 = New-Object System.Drawing.Font('Arial', 18)
$g.DrawString('Line one text sample', $font2, [System.Drawing.Brushes]::Black, 130, 220)
$g.DrawString('Line two text sample', $font2, [System.Drawing.Brushes]::Black, 130, 260)
$g.DrawString('Line three text sample', $font2, [System.Drawing.Brushes]::Black, 130, 300)

$bmp.Save($outputPath)
$g.Dispose()
$bmp.Dispose()
$font.Dispose()
$font2.Dispose()

Write-Host "Created $outputPath"
