$ErrorActionPreference = "Stop"
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class FG {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
}
"@
& powershell -NoProfile -ExecutionPolicy Bypass -File "D:\git\renderdoc-nikki\nikki\window_click.ps1" -Process "xstarter" -Activate 2>&1 | Select-Object -Last 2
Start-Sleep -Milliseconds 800
$fg = [FG]::GetForegroundWindow()
$fgPid = 0
[FG]::GetWindowThreadProcessId($fg, [ref]$fgPid) | Out-Null
$p = Get-Process -Id $fgPid -ErrorAction SilentlyContinue
Write-Output "foreground: pid=$fgPid name=$($p.ProcessName) title='$($p.MainWindowTitle)'"
if($p -and $p.ProcessName -eq "xstarter") { Write-Output "ACTIVATION OK" } else { Write-Output "ACTIVATION FAILED" }
