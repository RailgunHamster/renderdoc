Add-Type @"
using System;
using System.Runtime.InteropServices;
public class TW3 {
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint procId);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
}
"@

$hwnd = [IntPtr]::new(133624)
$procId = 0
$tid = [TW3]::GetWindowThreadProcessId($hwnd, [ref]$procId)
$ctid = [TW3]::GetCurrentThreadId()
Write-Host ("Target HWND={0} TID={1} PID={2} CurrentTID={3}" -f $hwnd, $tid, $procId, $ctid)

$isMin = [TW3]::IsIconic($hwnd)
Write-Host ("IsIconic={0}" -f $isMin)

if ($isMin) {
    [TW3]::ShowWindow($hwnd, 9) | Out-Null
    Start-Sleep -Milliseconds 200
}

[TW3]::AttachThreadInput($ctid, $tid, $true) | Out-Null
$r1 = [TW3]::SetForegroundWindow($hwnd)
Write-Host ("SetForegroundWindow1={0}" -f $r1)
Start-Sleep -Milliseconds 500

$fg = [TW3]::GetForegroundWindow()
Write-Host ("ForegroundWindow={0} (match={1})" -f $fg, $fg.Equals($hwnd))

[TW3]::AttachThreadInput($ctid, $tid, $false) | Out-Null
Start-Sleep -Milliseconds 500

$fg2 = [TW3]::GetForegroundWindow()
Write-Host ("ForegroundWindow2={0} (match={1})" -f $fg2, $fg2.Equals($hwnd))

$r2 = [TW3]::SetForegroundWindow($hwnd)
Write-Host ("SetForegroundWindow2={0}" -f $r2)
Start-Sleep -Milliseconds 500

$fg3 = [TW3]::GetForegroundWindow()
Write-Host ("ForegroundWindow3={0} (match={1})" -f $fg3, $fg3.Equals($hwnd))
