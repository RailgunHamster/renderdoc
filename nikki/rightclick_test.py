#!/usr/bin/env python3
"""Right-click test: activate xstarter, right-click the start button position.
If a context menu appears, simulated input works and the position is right."""
import ctypes
import sys
import time

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


def activate(win):
    user32.ShowWindow(win._hWnd, SW_RESTORE)
    user32.keybd_event(VK_MENU, 0, 0, 0)
    user32.keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0)
    for _ in range(5):
        user32.SetForegroundWindow(win._hWnd)
        time.sleep(0.15)
    user32.SetWindowPos(win._hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE)
    time.sleep(0.5)
    user32.SetWindowPos(win._hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE)


def main():
    win = find_window()
    if win is None:
        print("NO WINDOW", flush=True)
        return 1
    print(f"window: {win.width}x{win.height} @ ({win.left},{win.top})", flush=True)
    activate(win)
    time.sleep(0.8)
    win = find_window()
    bx = win.left + int(win.width * 0.8264)
    by = win.top + int(win.height * 0.8914)
    print(f"right-clicking at ({bx},{by})", flush=True)
    pyautogui.moveTo(bx, by, duration=0.4)
    time.sleep(0.3)
    pyautogui.click(bx, by, button='primary')
    print("right-clicked", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
