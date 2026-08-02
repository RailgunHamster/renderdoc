#!/usr/bin/env python3
"""Try to trigger the launcher's start button via keyboard navigation."""
import sys
import time
import subprocess

import pyautogui
import pygetwindow

pyautogui.FAILSAFE = True


def game_running():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq X6Game-Win64-Shipping.exe"],
                         capture_output=True, text=True).stdout
    return "X6Game-Win64-Shipping.exe" in out


def activate():
    for w in pygetwindow.getAllWindows():
        if w.title and "无限暖暖" in w.title:
            w.restore()
            try:
                w.activate()
            except Exception:
                pass
            time.sleep(1.0)
            return w
    return None


def main():
    win = activate()
    if win is None:
        print("NO WINDOW", flush=True)
        return 1

    strategies = [
        ("enter", lambda: pyautogui.press("enter")),
        ("tab+enter", lambda: (pyautogui.press("tab"), pyautogui.press("enter"))),
        ("tabx2+enter", lambda: (pyautogui.press("tab", presses=2), pyautogui.press("enter"))),
        ("tabx5+enter", lambda: (pyautogui.press("tab", presses=5), pyautogui.press("enter"))),
        ("tabx10+enter", lambda: (pyautogui.press("tab", presses=10), pyautogui.press("enter"))),
        ("space", lambda: pyautogui.press("space")),
    ]

    for name, fn in strategies:
        if game_running():
            print("GAME RUNNING", flush=True)
            return 0
        print(f"trying: {name}", flush=True)
        fn()
        time.sleep(3.0)

    print("RESULT: no game process after keyboard strategies", flush=True)
    return 1


if __name__ == "__main__":
    sys.exit(main())
