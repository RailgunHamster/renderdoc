import ctypes
import ctypes.wintypes as wt
import sys
import time

kernel32 = ctypes.windll.kernel32

PROCESS_ALL_ACCESS = 0x1F0FFF
MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000
PAGE_READWRITE = 0x04

dll_path = r"C:\Users\Administrator\game\InfinityNikki Launcher\InfinityNikki\rendertest.dll"

pid = int(sys.argv[1]) if len(sys.argv) > 1 else 0
if not pid:
    import subprocess
    out = subprocess.run(["powershell", "-NoProfile", "-Command",
        "Get-Process -Name 'X6Game-Win64-Shipping' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Id"],
        capture_output=True, text=True).stdout.strip()
    pid = int(out) if out else 0
if not pid:
    print("no X6Game process")
    sys.exit(1)

print(f"injecting into pid {pid}")
h = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
if not h:
    print(f"OpenProcess failed {ctypes.get_last_error()}")
    sys.exit(1)

path_b = dll_path.encode("utf-16-le") + b"\x00"
addr = kernel32.VirtualAllocEx(h, None, len(path_b), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
if not addr:
    print(f"VirtualAllocEx failed {ctypes.get_last_error()}")
    sys.exit(1)

written = ctypes.c_size_t()
if not kernel32.WriteProcessMemory(h, addr, path_b, len(path_b), ctypes.byref(written)):
    print(f"WriteProcessMemory failed {ctypes.get_last_error()}")
    sys.exit(1)

kernel32.GetModuleHandleW.restype = ctypes.c_void_p
kernel32.LoadLibraryW.restype = ctypes.c_void_p
loadlib = kernel32.GetProcAddress(kernel32.GetModuleHandleW("kernel32.dll"), b"LoadLibraryW")
print(f"LoadLibraryW at {loadlib:#x}")

tid = ctypes.c_ulong()
hthread = kernel32.CreateRemoteThread(h, None, 0, loadlib, addr, 0, ctypes.byref(tid))
if not hthread:
    print(f"CreateRemoteThread failed {ctypes.get_last_error()}")
    sys.exit(1)

kernel32.WaitForSingleObject(hthread, 15000)
mod = ctypes.c_void_p()
kernel32.GetExitCodeThread(hthread, ctypes.byref(mod))
print(f"thread exit = LoadLibraryW returned {mod.value:#x}")
if mod.value:
    print("INJECTED OK")
else:
    print("load failed (last error)", ctypes.get_last_error())
kernel32.CloseHandle(hthread)
kernel32.CloseHandle(h)
