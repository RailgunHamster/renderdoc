/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2015-2026 Baldur Karlsson
 * Copyright (c) 2014 Crytek
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

// win32_libentry.cpp : Defines the entry point for the DLL
#include <tchar.h>
#include <windows.h>
#include <dbghelp.h>
#include "common/common.h"
#include "core/core.h"
#include "hooks/hooks.h"
#include "strings/string_utils.h"

extern "C" void InstallDXGIFactoryInlineHooks();
extern "C" void *GetWrappedD3D12Device();


// Vectored exception handler that logs the crash location and writes a
// minidump. Registered as early as possible in DllMain so that it fires for
// any exception in the target process (the game's own UE crash handler and
// WER do not produce usable dumps here; procdump cannot attach - access denied).
static LONG WINAPI CrashDumpHandler(PEXCEPTION_POINTERS ep)
{
  // Benign debugger-information exceptions that games fire routinely
  // (OutputDebugString, SetThreadName, CLR notifications, ...). These are NOT
  // crashes: writing an 8GB full-memory dump every 5s for these froze the game
  // (disk I/O storm). Ignore them entirely.
  DWORD code = ep->ExceptionRecord->ExceptionCode;
  if(code == 0x4001000A || /* DBG_PRINTEXCEPTION_C */ code == 0x406D1388 || /* MS_VC_EXCEPTION */
     code == 0x40000010 || /* DBG_TERMINATE_THREAD */ code == 0x40010006 /* DBG_PRINTEXCEPTION_WIDE_C */
     )
    return EXCEPTION_CONTINUE_SEARCH;

  // rate-limit: one dump per 5s per process
  static volatile LONG lastDump = 0;
  LONG now = (LONG)GetTickCount();
  LONG prev = InterlockedExchange(&lastDump, now);
  if(now - prev < 5000)
    return EXCEPTION_CONTINUE_SEARCH;

  {
    void *addr = ep->ExceptionRecord->ExceptionAddress;
    HMODULE mod = NULL;
    char modname[256] = "?";
    if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCSTR)addr, &mod))
    {
      GetModuleFileNameA(mod, modname, sizeof(modname));
    }

    FILE *f = NULL;
    fopen_s(&f, "D:\\git\\renderdoc-nikki\\nikki\\crash_log.txt", "a");
    if(f)
    {
      fprintf(f, "pid %d t=%llu code=0x%08X at %s+0x%llX (addr %p) info0=0x%llX\n",
              (int)GetCurrentProcessId(), (unsigned long long)GetTickCount64(),
              ep->ExceptionRecord->ExceptionCode,
              strrchr(modname, '\\') ? strrchr(modname, '\\') + 1 : modname,
              mod ? (unsigned long long)((uintptr_t)addr - (uintptr_t)mod) : 0, addr,
              (unsigned long long)ep->ExceptionRecord->ExceptionInformation[0]);

      // capture the call stack (module + offset) for diagnosis
      void *stack[32] = {0};
      unsigned short frames =
          CaptureStackBackTrace(0, ARRAY_COUNT(stack), stack, NULL);
      fprintf(f, "  stack (%u frames):\n", (unsigned int)frames);
      for(unsigned short i = 0; i < frames && i < ARRAY_COUNT(stack); i++)
      {
        HMODULE smod = NULL;
        char smodname[256] = "?";
        if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                  GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCSTR)stack[i], &smod))
        {
          GetModuleFileNameA(smod, smodname, sizeof(smodname));
        }
        fprintf(f, "    [%02u] %s+0x%llX (addr %p)\n", (unsigned int)i,
                strrchr(smodname, '\\') ? strrchr(smodname, '\\') + 1 : smodname,
                smod ? (unsigned long long)((uintptr_t)stack[i] - (uintptr_t)smod) : 0, stack[i]);
      }
      fclose(f);
    }
  }

  wchar_t path[MAX_PATH];
  wsprintfW(path, L"D:\\git\\renderdoc-nikki\\nikki\\dumps\\crash_%d_%d.dmp",
            (int)GetCurrentProcessId(), (int)now);

  HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
  if(dbghelp)
  {
    typedef BOOL(WINAPI *PFN_MiniDumpWriteDump)(HANDLE, DWORD, HANDLE, DWORD, PVOID, PVOID, PVOID);
    PFN_MiniDumpWriteDump mdwd =
        (PFN_MiniDumpWriteDump)GetProcAddress(dbghelp, "MiniDumpWriteDump");
    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                               NULL);
    if(hFile != INVALID_HANDLE_VALUE)
    {
      MINIDUMP_EXCEPTION_INFORMATION mei = {GetCurrentThreadId(), ep, FALSE};
      if(mdwd)
        mdwd(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpNormal, &mei, NULL, NULL);
      CloseHandle(hFile);
    }
  }

  return EXCEPTION_CONTINUE_SEARCH;
}

static BOOL add_hooks()
{
  wchar_t curFile[512];
  GetModuleFileNameW(NULL, curFile, 512);

  rdcstr f = get_basename(strlower(StringFormat::Wide2UTF8(curFile)));

  // bail immediately if we're in a system process. We don't want to hook, log, anything -
  // this instance is being used for a shell extension.
  if(f == "dllhost.exe" || f == "explorer.exe")
  {
#if ENABLED(RDOC_RELEASE)
    OutputDebugStringA(
        "Detecting shell process! Disabling hooking in dllhost.exe or explorer.exe\n");
#endif
    return TRUE;
  }

  // search for an exported symbol with this name, typically RENDERTEST__replay__marker
  if(LibraryHooks::Detect(STRINGIZE(RDOC_BASE_NAME) "__replay__marker"))
  {
    RDCDEBUG("Not creating hooks - in replay app");

    RenderTest::Inst().SetReplayApp(true);

    RenderTest::Inst().Initialise();

    LibraryHooks::ReplayInitialise();

    return true;
  }

  char *noInit = NULL;
  size_t initLen = 0;
  _dupenv_s(&noInit, &initLen, "RENDERTEST_NO_INIT");
  if(noInit && noInit[0] == '1')
  {
    free(noInit);
    return TRUE;
  }
  free(noInit);

  // Bisection switch: skip ALL initialisation (no RenderTest::Inst() use at all)
  if(GetFileAttributesA("D:\\git\\renderdoc-nikki\\nikki\\nikkiproxy_skip_init.txt") !=
     INVALID_FILE_ATTRIBUTES)
  {
    RDCLOG("Initialisation skipped by file switch");
    return TRUE;
  }

  char *delayHooks = NULL;
  size_t len = 0;
  _dupenv_s(&delayHooks, &len, "RENDERTEST_DELAY_HOOKS");
  bool delayMode = (delayHooks && delayHooks[0] == '1');
  free(delayHooks);

  RenderTest::Inst().Initialise();

  RDCLOG("Loading into %ls", curFile);

  if(delayMode)
  {
    RDCLOG("RENDERTEST_DELAY_HOOKS=1, skipping hook registration (delayed mode)");
    return TRUE;
  }

  // Allow capture configuration via environment variables, so that the core can
  // be loaded by a proxy DLL without going through RenderTestcmd's injection path.
  rdcstr capturefile = Process::GetEnvVariable("RENDERTEST_CAPFILE");
  rdcstr opts = Process::GetEnvVariable("RENDERTEST_CAPOPTS");

  if(!opts.empty())
  {
    CaptureOptions optstruct;
    optstruct.DecodeFromString(opts);

    RenderTest::Inst().SetCaptureOptions(optstruct);
  }

  if(!capturefile.empty())
  {
    RenderTest::Inst().SetCaptureFileTemplate(capturefile);
  }

  // Bisection switch: run Initialise but skip ALL hook registration (file marker)
  // Applies only to the game process: the launcher/bootstrap still registers
  // hooks (including the inline CreateProcess hook) so children get injected.
  // The game process instead polls for a trigger file and installs hooks
  // later, once the game is fully up (ACE freezes the game if hooks are
  // registered too early in the game process).
  {
    wchar_t curExe[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, curExe, MAX_PATH - 1);
    wchar_t *name = wcsrchr(curExe, L'\\');
    bool isGame =
        (name && _wcsicmp(name + 1, L"X6Game-Win64-Shipping.exe") == 0);
    if(isGame &&
       GetFileAttributesA("D:\\git\\renderdoc-nikki\\nikki\\nikkiproxy_skip_registerhooks.txt") !=
           INVALID_FILE_ATTRIBUTES)
    {
      RDCLOG("Hook registration skipped by file switch (game process) - polling for delayed install");

      // Install the DXGI factory inline hooks immediately: the game creates its
      // DXGI factory during early startup, before the delayed full hook
      // registration kicks in, so this needs to happen from DllMain.
      InstallDXGIFactoryInlineHooks();

      static bool delayedThreadStarted = false;
      if(!delayedThreadStarted)
      {
        delayedThreadStarted = true;
        CreateThread(
            NULL, 0,
            [](LPVOID) -> DWORD {
              // Auto-install 2 seconds after attach (the game creates the DXGI
              // factory and the D3D12 device a few seconds later, so the hooks
              // are in place before them) or immediately when the trigger file
              // appears.
              // Auto-install 2 seconds after attach (the game creates the
              // D3D12 device a few seconds later, so the hooks are in place
              // before it) or immediately when the trigger file appears.
              // Keep polling afterwards: the game delay-loads dxgi.dll well
              // after startup, and the factory inline hooks need to go in
              // before CreateDXGIFactory is called.
              DWORD start = GetTickCount();
              bool hooksDone = false;
              static bool capStarted = false;
              for(;;)
              {
                InstallDXGIFactoryInlineHooks();

                // capture trigger via file markers. The trigger files are
                // deleted after being consumed so they fire only once.
                if(GetFileAttributesA("D:\\git\\renderdoc-nikki\\nikki\\nikkiproxy_capture_start.txt") !=
                       INVALID_FILE_ATTRIBUTES &&
                   !capStarted)
                {
                  capStarted = true;
                  DeleteFileA("D:\\git\\renderdoc-nikki\\nikki\\nikkiproxy_capture_start.txt");
                  RDCLOG("File-triggered capture START");
                  {
                    FILE *f = NULL;
                    fopen_s(&f, "D:\\git\\renderdoc-nikki\\nikki\\marker_capture.txt", "a");
                    if(f)
                    {
                      fprintf(f, "pid %d capture START triggered\n", (int)GetCurrentProcessId());
                      fclose(f);
                    }
                  }
                  
                  void *dev = GetWrappedD3D12Device();
                  RenderTest::Inst().StartFrameCapture(DeviceOwnedWindow(dev, NULL));
                  {
                    FILE *f = NULL;
                    fopen_s(&f, "D:\\git\\renderdoc-nikki\\nikki\\marker_capture.txt", "a");
                    if(f)
                    {
                      fprintf(f, "pid %d after Start: dev=%p isCapturing=%d\n",
                              (int)GetCurrentProcessId(), dev,
                              RenderTest::Inst().IsFrameCapturing() ? 1 : 0);
                      fclose(f);
                    }
                  }
                }
                if(GetFileAttributesA("D:\\git\\renderdoc-nikki\\nikki\\nikkiproxy_capture_end.txt") !=
                       INVALID_FILE_ATTRIBUTES &&
                   capStarted)
                {
                  capStarted = false;
                  DeleteFileA("D:\\git\\renderdoc-nikki\\nikki\\nikkiproxy_capture_end.txt");
                  RDCLOG("File-triggered capture END");
                  {
                    FILE *f = NULL;
                    fopen_s(&f, "D:\\git\\renderdoc-nikki\\nikki\\marker_capture.txt", "a");
                    if(f)
                    {
                      fprintf(f, "pid %d capture END triggered\n", (int)GetCurrentProcessId());
                      fclose(f);
                    }
                  }
                  
                  void *dev = GetWrappedD3D12Device();
                  RenderTest::Inst().EndFrameCapture(DeviceOwnedWindow(dev, NULL));
                }

                if(!hooksDone &&
                   (GetFileAttributesA("D:\\git\\renderdoc-nikki\\nikki\\nikkiproxy_install_hooks_now.txt") !=
                        INVALID_FILE_ATTRIBUTES ||
                    (GetTickCount() - start) > 2000))
                {
                  RDCLOG("Delayed hook installation triggered");
                  LibraryHooks::RegisterHooks();
                  hooksDone = true;
                }
                Sleep(250);
              }
            },
            NULL, 0, NULL);
      }
      return TRUE;
    }
  }

  LibraryHooks::RegisterHooks();

  return TRUE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
  if(ul_reason_for_call == DLL_PROCESS_ATTACH)
  {
    // register a crash dump handler as early as possible
    AddVectoredExceptionHandler(1, CrashDumpHandler);

    // debug marker: record exact attach time (GetTickCount64) so we can compare
    // against device creation / hook registration timestamps in the target
    {
      FILE *f = NULL;
      fopen_s(&f, "D:\\git\\renderdoc-nikki\\nikki\\marker_dllmain.txt", "a");
      if(f)
      {
        fprintf(f, "DllMain attach pid %d t=%llu\n", (int)GetCurrentProcessId(),
                (unsigned long long)GetTickCount64());
        fclose(f);
      }
    }

    BOOL ret = add_hooks();
    SetLastError(0);
    return ret;
  }

  return TRUE;
}
