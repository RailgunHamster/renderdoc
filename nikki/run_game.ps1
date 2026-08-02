$ErrorActionPreference = "Stop"
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W32 {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$proc = Get-Process -Name "xstarter" | Select-Object -First 1
if(-not $proc) { Write-Error "xstarter not running"; exit 1 }
$rect = New-Object W32+RECT
[W32]::GetWindowRect($proc.MainWindowHandle, [ref]$rect) | Out-Null
$w = $rect.Right - $rect.Left
$h = $rect.Bottom - $rect.Top
# button center at 82.64% / 89.14% of window (from measured 2880x1620 logical layout)
$bx = [int]($rect.Left + $w * 0.8264)
$by = [int]($rect.Top + $h * 0.8914)
"window ${w}x${h} at ($($rect.Left),$($rect.Top)), clicking start-game at ($bx,$by) t=$(Get-Date -Format HH:mm:ss)"
& powershell -NoProfile -ExecutionPolicy Bypass -File "D:\git\rendertst-nikki\nikki\window_click.ps1" -Process "xstarter" -X $($bx - $rect.Left) -Y $($by - $rect.Top) 2>&1 | Select-Object -Last 2
