import ctypes
import time
import pyautogui
import pygetwindow

pyautogui.FAILSAFE = False
user32 = ctypes.windll.user32

win = None
for w in pygetwindow.getAllWindows():
    if w.title and "无限暖暖" in w.title:
        win = w
        break
if not win:
    print("no window")
    raise SystemExit

user32.SetForegroundWindow(win._hWnd)
time.sleep(1)
bx = int(win.left + win.width * 0.8264)
by = int(win.top + win.height * 0.8914)
print(f"clicking ({bx},{by})")
pyautogui.click(bx, by, button="primary")
time.sleep(3)
for i in range(3):
    print("attempt", i + 1)
    pyautogui.click(bx, by, button="primary")
    time.sleep(5)
