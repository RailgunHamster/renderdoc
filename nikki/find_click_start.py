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
    print("NO WINDOW")
    raise SystemExit

# activate launcher to foreground
user32.ShowWindow(win._hWnd, 9)
time.sleep(1)
user32.SetForegroundWindow(win._hWnd)
time.sleep(0.5)
user32.keybd_event(0x12, 0, 0, 0)
user32.keybd_event(0x12, 0, 2, 0)
user32.SetForegroundWindow(win._hWnd)
time.sleep(1)
print(f"window rect: {win.left},{win.top} {win.width}x{win.height}")

# locate the start button template on screen
for scale in (1.0, 0.9, 0.8, 0.75, 0.67, 0.56, 0.5):
    img = "D:\\git\\rendertst-nikki\\nikki\\start_game.png"
    try:
        pos = pyautogui.locateOnScreen(img, confidence=0.8, grayscale=False)
        if pos:
            cx = pos.left + pos.width // 2
            cy = pos.top + pos.height // 2
            print(f"FOUND at scale={scale}: ({cx},{cy}) size={pos.width}x{pos.height}")
            pyautogui.click(cx, cy, button="primary")
            print("clicked")
            break
    except Exception as e:
        print(f"scale {scale}: {type(e).__name__}: {e}")
else:
    print("NOT FOUND on screen")
