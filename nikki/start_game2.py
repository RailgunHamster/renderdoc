#!/usr/bin/env python3
"""Activate xstarter reliably (Alt-unlock + SetForegroundWindow + TOPMOST),
verify it is foreground, then click the start button with pyautogui."""
import ctypes
import sys
import time
import subprocess

import pyautogui
import pygetwindow

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

SW_RESTORE = 9
HWND_TOPMOST = -1
HWND_NOTOPMOST = -2
SWP_NOMOVE = 0x0002
SWP_NOSIZE = 0x0001
VK_MENU = 0x12
KEYEVENTF_KEYUP = 0x0002


def find_window():
    for w in pygetwindow.getAllWindows():
        if w.title and "无限暖暖" in w.title:
            return w
    return None


def foreground_pid():
    hwnd = user32.GetForegroundWindow()
    pid = ctypes.c_ulong()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    return pid.value, hwnd


def activate(win):
    user32.ShowWindow(win._hWnd, SW_RESTORE)
    # Alt keypress unlocks the foreground-lock so SetForegroundWindow works
    user32.keybd_event(VK_MENU, 0, 0, 0)
    user32.keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0)
    for _ in range(5):
        user32.SetForegroundWindow(win._hWnd)
        time.sleep(0.15)
    user32.SetWindowPos(win._hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE)
    time.sleep(0.5)
    user32.SetWindowPos(win._hWnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE)


def game_running():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq X6Game-Win64-Shipping.exe"],
                         capture_output=True, text=True).stdout
    return "X6Game-Win64-Shipping.exe" in out


def main():
    win = find_window()
    if win is None:
        print("NO WINDOW", flush=True)
        return 1

    print(f"window: {win.width}x{win.height} @ ({win.left},{win.top})", flush=True)
    fg_pid, fg_hwnd = foreground_pid()
    print(f"foreground before: pid={fg_pid} (target pid={win._hWnd})", flush=True)

    activate(win)
    time.sleep(0.8)

    fg_pid, fg_hwnd = foreground_pid()
    print(f"foreground after: pid={fg_pid} hwnd={fg_hwnd} target_hwnd={win._hWnd}", flush=True)

    # re-read geometry
    win = find_window()
    bx = win.left + int(win.width * 0.8264)
    by = win.top + int(win.height * 0.8914)
    print(f"clicking start at ({bx},{by})", flush=True)

    for i in range(3):
        pyautogui.moveTo(bx, by, duration=0.4)
        time.sleep(0.3)
        pyautogui.click(bx, by, button='primary')
        print(f"clicked attempt {i+1}", flush=True)
        time.sleep(2.5)
        if game_running():
            print("GAME PROCESS DETECTED", flush=True)
            return 0

    print("RESULT: no game process", flush=True)
    return 1


if __name__ == "__main__":
    sys.exit(main())
