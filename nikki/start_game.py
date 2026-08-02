#!/usr/bin/env python3
"""Start the InfinityNikki game by clicking the launcher's start button.
Button is at 82.64% / 89.14% of the xstarter window (measured via UIA).
"""
import sys
import time

import pyautogui
import pygetwindow

pyautogui.FAILSAFE = True
pyautogui.PAUSE = 0.05


def find_window():
    for w in pygetwindow.getAllWindows():
        if w.title and "无限暖暖" in w.title:
            return w
    return None


def click_start(retries=3):
    win = find_window()
    if win is None:
        print("NO xstarter window found", flush=True)
        return False

    win.restore()
    try:
        win.activate()
    except Exception as e:
        print(f"activate error: {e}", flush=True)

    time.sleep(1.2)

    # re-read geometry after activation
    win = find_window()
    bx = win.left + int(win.width * 0.8264)
    by = win.top + int(win.height * 0.8914)
    print(f"window {win.width}x{win.height} @ ({win.left},{win.top}); "
          f"clicking start at ({bx},{by})", flush=True)

    for i in range(retries):
        pyautogui.moveTo(bx, by, duration=0.4)
        time.sleep(0.3)
        pyautogui.click(bx, by)
        print(f"clicked ({bx},{by}) attempt {i+1}", flush=True)
        time.sleep(2.0)
        # if a game process appeared, stop clicking
        import subprocess
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq X6Game-Win64-Shipping.exe"],
                             capture_output=True, text=True).stdout
        if "X6Game-Win64-Shipping.exe" in out:
            print("game process detected!", flush=True)
            return True
    return False


if __name__ == "__main__":
    ok = click_start()
    print("RESULT:", "STARTED" if ok else "NO_GAME_PROCESS", flush=True)
    sys.exit(0 if ok else 1)
