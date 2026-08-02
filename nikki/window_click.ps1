Add-Type @"
using System;
using System.Runtime.InteropServices;

public class Win32 {
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder lpString, int nMaxCount);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint procId);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern IntPtr SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int X, int Y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, int dx, int dy, uint cButtons, uint dwExtraInfo);
    [DllImport("user32.dll")] public static extern uint SendMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool SwitchToThisWindow(IntPtr hWnd, bool fAltTab);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    public static readonly IntPtr HWND_TOPMOST = new IntPtr(-1);
    public static readonly IntPtr HWND_NOTOPMOST = new IntPtr(-2);
    public static readonly IntPtr HWND_TOP = IntPtr.Zero;

    public const int SW_RESTORE = 9;
    public const int SW_SHOW = 5;
    public const uint SWP_NOMOVE = 0x0002;
    public const uint SWP_NOSIZE = 0x0001;
    public const uint SWP_SHOWWINDOW = 0x0040;
    public const uint SWP_NOACTIVATE = 0x0010;
    public const uint MOUSEEVENTF_LEFTDOWN = 0x02;
    public const uint MOUSEEVENTF_LEFTUP = 0x04;
    public const uint WM_LBUTTONDOWN = 0x0201;
    public const uint WM_LBUTTONUP = 0x0202;
}
"@

$ErrorActionPreference = "Stop"

function Find-Window {
    param(
        [string]$TitlePattern,
        [string]$ProcessName
    )
    $foundList = [System.Collections.Generic.List[PSCustomObject]]::new()
    $callback = [Win32+EnumWindowsProc]{
        param($hWnd, $lParam)
        if (-not [Win32]::IsWindowVisible($hWnd)) { return $true }
        $sb = New-Object System.Text.StringBuilder 256
        $len = [Win32]::GetWindowText($hWnd, $sb, 256)
        $title = $sb.ToString()
        if ($len -eq 0 -or [string]::IsNullOrEmpty($title)) { return $true }

        $procId = 0
        [Win32]::GetWindowThreadProcessId($hWnd, [ref]$procId) | Out-Null
        $procName = ""
        try {
            $p = Get-Process -Id $procId -ErrorAction SilentlyContinue
            $procName = $p.ProcessName
        } catch {}

        $match = $true
        if ($TitlePattern -and $TitlePattern -ne "*") {
            $match = $match -and ($title -like "*$TitlePattern*")
        }
        if ($ProcessName -and $ProcessName -ne "*") {
            $match = $match -and ($procName -like "*$ProcessName*")
        }

        if ($match) {
            $rect = New-Object Win32+RECT
            $grResult = [Win32]::GetWindowRect($hWnd, [ref]$rect)
            $foundList.Add([PSCustomObject]@{
                HWND      = $hWnd
                Title     = $title
                Process   = $procName
                PID       = $procId
                Left      = $rect.Left
                Top       = $rect.Top
                Right     = $rect.Right
                Bottom    = $rect.Bottom
                Width     = $rect.Right - $rect.Left
                Height    = $rect.Bottom - $rect.Top
            }) | Out-Null
        }
        return $true
    }

    [Win32]::EnumWindows($callback, [IntPtr]::Zero) | Out-Null
    return $foundList.ToArray()
}

function Activate-Window {
    param([IntPtr]$hWnd)
    $procId = 0
    $targetThreadId = [Win32]::GetWindowThreadProcessId($hWnd, [ref]$procId)
    $currentThreadId = [Win32]::GetCurrentThreadId()

    $isMinimized = [Win32]::IsIconic($hWnd)
    if ($isMinimized) {
        [Win32]::ShowWindow($hWnd, [Win32]::SW_RESTORE) | Out-Null
        Start-Sleep -Milliseconds 300
    }

    [Win32]::AttachThreadInput($currentThreadId, $targetThreadId, $true) | Out-Null
    [Win32]::SetForegroundWindow($hWnd) | Out-Null

    [Win32]::SetWindowPos($hWnd, [Win32]::HWND_TOPMOST, 0, 0, 0, 0, [Win32]::SWP_NOMOVE -bor [Win32]::SWP_NOSIZE) | Out-Null
    Start-Sleep -Milliseconds 1500

    [Win32]::AttachThreadInput($currentThreadId, $targetThreadId, $false) | Out-Null
    [Win32]::SetWindowPos($hWnd, [Win32]::HWND_NOTOPMOST, 0, 0, 0, 0, [Win32]::SWP_NOMOVE -bor [Win32]::SWP_NOSIZE) | Out-Null
    Start-Sleep -Milliseconds 200
    [Win32]::SwitchToThisWindow($hWnd, $true) | Out-Null
}

function Click-WindowArea {
    param(
        [IntPtr]$hWnd,
        [int]$RelativeX,
        [int]$RelativeY,
        [switch]$SendMessage,
        [switch]$DoubleClick
    )
    $rect = New-Object Win32+RECT
    [Win32]::GetWindowRect($hWnd, [ref]$rect) | Out-Null
    $absX = $rect.Left + $RelativeX
    $absY = $rect.Top + $RelativeY

    if ($SendMessage) {
        $lParam = [IntPtr]::new($absX -bor ($absY -shl 16))
        [Win32]::SendMessage($hWnd, [Win32]::WM_LBUTTONDOWN, [IntPtr]::new(1), $lParam) | Out-Null
        [Win32]::SendMessage($hWnd, [Win32]::WM_LBUTTONUP, [IntPtr]::new(0), $lParam) | Out-Null
    } else {
        Activate-Window -hWnd $hWnd
        [Win32]::SetCursorPos($absX, $absY) | Out-Null
        Start-Sleep -Milliseconds 50
        [Win32]::mouse_event([Win32]::MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0) | Out-Null
        Start-Sleep -Milliseconds 30
        [Win32]::mouse_event([Win32]::MOUSEEVENTF_LEFTUP, 0, 0, 0, 0) | Out-Null
    }

    if ($DoubleClick) {
        Start-Sleep -Milliseconds 80
        if ($SendMessage) {
            [Win32]::SendMessage($hWnd, [Win32]::WM_LBUTTONDOWN, [IntPtr]::new(1), $lParam) | Out-Null
            [Win32]::SendMessage($hWnd, [Win32]::WM_LBUTTONUP, [IntPtr]::new(0), $lParam) | Out-Null
        } else {
            [Win32]::mouse_event([Win32]::MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0) | Out-Null
            Start-Sleep -Milliseconds 30
            [Win32]::mouse_event([Win32]::MOUSEEVENTF_LEFTUP, 0, 0, 0, 0) | Out-Null
        }
    }

    Write-Host "Clicked at ($RelativeX, $RelativeY) -> screen ($absX, $absY)"
}

function Select-Window {
    param(
        [string]$TitlePattern,
        [string]$ProcessName
    )
    $wins = @(Find-Window -TitlePattern $TitlePattern -ProcessName $ProcessName)
    if (-not $wins) {
        Write-Error "No window found (TitlePattern='$TitlePattern', ProcessName='$ProcessName')"
    }
    if ($wins.Count -eq 1) { return $wins[0] }

    $largest = $wins | Sort-Object { $_.Width * $_.Height } -Descending | Select-Object -First 1
    if ($ProcessName -and -not $TitlePattern) {
        Write-Host ("Multiple windows for process '{0}', auto-selected largest: [{1}] - `"{2}`" ({3}x{4})" -f $ProcessName, $largest.Process, $largest.Title, $largest.Width, $largest.Height)
        return $largest
    }

    Write-Host "`nMultiple windows found. Please select:"
    for ($i = 0; $i -lt $wins.Count; $i++) {
        Write-Host ("  [{0}] [{1}] - `"{2}`" - ({3},{4}) {5}x{6}" -f $i, $wins[$i].Process, $wins[$i].Title, $wins[$i].Left, $wins[$i].Top, $wins[$i].Width, $wins[$i].Height)
    }
    $idx = Read-Host "`nEnter index"
    if ($idx -match "^\d+$" -and [int]$idx -lt $wins.Count) {
        return $wins[[int]$idx]
    } else {
        Write-Error "Invalid selection"
    }
}

function Show-Help {
    Write-Host @"
Usage: window_click.ps1 [options]

Options:
  -Title <pattern>    Window title to match (wildcard * supported)
  -Process <name>     Process name to match (without .exe)
  -X <pixels>         X offset from window top-left
  -Y <pixels>         Y offset from window top-left
  -Activate           Bring window to foreground
  -SendMessage        Use SendMessage (doesn't move mouse/activate)
  -DoubleClick        Perform a double-click
  -List               List all visible windows
  -Help               Show this help

Examples:
  .\window_click.ps1 -Title "*chrome*" -X 100 -Y 50 -Activate
  .\window_click.ps1 -Process "notepad" -X 10 -Y 10 -SendMessage
  .\window_click.ps1 -List
"@
}

function List-Windows {
    $wins = Find-Window -TitlePattern "*" -ProcessName "*"
    if (-not $wins) { Write-Host "No visible windows found."; return }
    Write-Host ("{0,-5} {1,-25} {2,-10} {3}" -f "Idx", "Process", "PID", "Title")
    Write-Host ("-" * 80)
    for ($i = 0; $i -lt $wins.Count; $i++) {
        Write-Host ("{0,-5} {1,-25} {2,-10} {3}" -f $i, $wins[$i].Process, $wins[$i].PID, $wins[$i].Title)
    }
}

$TitlePattern = ""
$ProcessName = ""
$X = 0; $Y = 0
$Activate = $false
$SendMessage = $false
$DoubleClick = $false
$ListMode = $false
$params = @()
for ($i = 0; $i -lt $args.Length; $i++) {
    switch ($args[$i]) {
        "-Title"       { $i++; $TitlePattern = $args[$i] }
        "-Process"     { $i++; $ProcessName = $args[$i] }
        "-X"           { $i++; $X = [int]$args[$i] }
        "-Y"           { $i++; $Y = [int]$args[$i] }
        "-Activate"    { $Activate = $true }
        "-SendMessage" { $SendMessage = $true }
        "-DoubleClick" { $DoubleClick = $true }
        "-List"        { $ListMode = $true }
        "-Help"        { Show-Help; exit 0 }
        default        { $params += $args[$i] }
    }
}

if ($ListMode) {
    List-Windows
    exit 0
}

if (-not $TitlePattern -and -not $ProcessName) {
    Write-Error "Must specify -Title or -Process. Use -Help for usage."
}

$win = Select-Window -TitlePattern $TitlePattern -ProcessName $ProcessName
Write-Host ("`nTarget: [{0}] - `"{1}`" ({2}x{3} @ ({4},{5}))" -f $win.Process, $win.Title, $win.Width, $win.Height, $win.Left, $win.Top)

if ($Activate -and -not $SendMessage) {
    Activate-Window -hWnd $win.HWND
    Write-Host "窗口已激活。"
    exit 0
}

Click-WindowArea -hWnd $win.HWND -RelativeX $X -RelativeY $Y -SendMessage:$SendMessage -DoubleClick:$DoubleClick
