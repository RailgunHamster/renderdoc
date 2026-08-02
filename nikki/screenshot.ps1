Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class CapWin {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$proc = Get-Process -Name "xstarter" | Select-Object -First 1
if(-not $proc) { Write-Error "xstarter not running"; exit 1 }
$h = $proc.MainWindowHandle
[CapWin]::ShowWindow($h, 9) | Out-Null
[CapWin]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 1200
$rect = New-Object CapWin+RECT
[CapWin]::GetWindowRect($h, [ref]$rect) | Out-Null
$w = $rect.Right - $rect.Left
$ht = $rect.Bottom - $rect.Top
"window rect: ($($rect.Left),$($rect.Top)) ${w}x${ht}"
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
$bmp.Save("D:\git\renderdoc-nikki\nikki\xstarter_shot.png", [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"saved xstarter_shot.png"
