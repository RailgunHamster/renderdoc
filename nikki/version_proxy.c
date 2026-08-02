// VERSION.dll proxy for Infinity Nikki bootstrap (InfinityNikki.exe)
// Loads rendertest.dll with hookIntoChildren so the game process gets injected
// when the bootstrap launches it. All VERSION exports are trampoline-forwarded
// to version_real.dll (a copy of the system version.dll).
#include <windows.h>
#include <stdio.h>

static HMODULE g_real = NULL;

static void ProxyLog(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  FILE *f = NULL;
  fopen_s(&f, "D:\\git\\renderdoc-nikki\\nikki\\version_proxy.log", "a");
  if(f)
  {
    fprintf(f, "%d: ", (int)GetCurrentProcessId());
    vfprintf(f, fmt, args);
    fprintf(f, "\n");
    fclose(f);
  }
  va_end(args);
}

#define FWD0(name, ret) \
  ret __stdcall name(void) \
  { \
    static FARPROC p = NULL; \
    if(!p) p = GetProcAddress(g_real, #name); \
    return ((ret(__stdcall *)(void))p)(); \
  }

#define FWD1(name, ret, t1) \
  ret __stdcall name(t1 a) \
  { \
    static FARPROC p = NULL; \
    if(!p) p = GetProcAddress(g_real, #name); \
    return ((ret(__stdcall *)(t1))p)(a); \
  }

#define FWD2(name, ret, t1, t2) \
  ret __stdcall name(t1 a, t2 b) \
  { \
    static FARPROC p = NULL; \
    if(!p) p = GetProcAddress(g_real, #name); \
    return ((ret(__stdcall *)(t1, t2))p)(a, b); \
  }

#define FWD3(name, ret, t1, t2, t3) \
  ret __stdcall name(t1 a, t2 b, t3 c) \
  { \
    static FARPROC p = NULL; \
    if(!p) p = GetProcAddress(g_real, #name); \
    return ((ret(__stdcall *)(t1, t2, t3))p)(a, b, c); \
  }

#define FWD4(name, ret, t1, t2, t3, t4) \
  ret __stdcall name(t1 a, t2 b, t3 c, t4 d) \
  { \
    static FARPROC p = NULL; \
    if(!p) p = GetProcAddress(g_real, #name); \
    return ((ret(__stdcall *)(t1, t2, t3, t4))p)(a, b, c, d); \
  }

#define FWD5(name, ret, t1, t2, t3, t4, t5) \
  ret __stdcall name(t1 a, t2 b, t3 c, t4 d, t5 e) \
  { \
    static FARPROC p = NULL; \
    if(!p) p = GetProcAddress(g_real, #name); \
    return ((ret(__stdcall *)(t1, t2, t3, t4, t5))p)(a, b, c, d, e); \
  }

#define FWD7(name, ret, t1, t2, t3, t4, t5, t6, t7) \
  ret __stdcall name(t1 a, t2 b, t3 c, t4 d, t5 e, t6 f, t7 g) \
  { \
    static FARPROC p = NULL; \
    if(!p) p = GetProcAddress(g_real, #name); \
    return ((ret(__stdcall *)(t1, t2, t3, t4, t5, t6, t7))p)(a, b, c, d, e, f, g); \
  }

#define FWD8(name, ret, t1, t2, t3, t4, t5, t6, t7, t8) \
  ret __stdcall name(t1 a, t2 b, t3 c, t4 d, t5 e, t6 f, t7 g, t8 h) \
  { \
    static FARPROC p = NULL; \
    if(!p) p = GetProcAddress(g_real, #name); \
    return ((ret(__stdcall *)(t1, t2, t3, t4, t5, t6, t7, t8))p)(a, b, c, d, e, f, g, h); \
  }

FWD4(GetFileVersionInfoA, BOOL, LPCSTR, DWORD, DWORD, LPVOID)
FWD3(GetFileVersionInfoByHandle, BOOL, HANDLE, DWORD, LPVOID)
FWD5(GetFileVersionInfoExA, BOOL, DWORD, LPCSTR, DWORD, DWORD, LPVOID)
FWD5(GetFileVersionInfoExW, BOOL, DWORD, LPCWSTR, DWORD, DWORD, LPVOID)
FWD2(GetFileVersionInfoSizeA, DWORD, LPCSTR, LPDWORD)
FWD3(GetFileVersionInfoSizeExA, DWORD, DWORD, LPCSTR, LPDWORD)
FWD3(GetFileVersionInfoSizeExW, DWORD, DWORD, LPCWSTR, LPDWORD)
FWD2(GetFileVersionInfoSizeW, DWORD, LPCWSTR, LPDWORD)
FWD4(GetFileVersionInfoW, BOOL, LPCWSTR, DWORD, DWORD, LPVOID)
FWD7(VerFindFileA, DWORD, DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR)
FWD7(VerFindFileW, DWORD, DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR)
FWD8(VerInstallFileA, DWORD, DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR)
FWD8(VerInstallFileW, DWORD, DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR)
FWD4(VerQueryValueA, BOOL, LPCSTR, LPCSTR, LPVOID *, PUINT)
FWD4(VerQueryValueW, BOOL, LPCWSTR, LPCWSTR, LPVOID *, PUINT)

// VerLanguageNameA/W are forwarded by the system dll to KERNEL32
FWD1(VerLanguageNameA, DWORD, DWORD)
FWD1(VerLanguageNameW, DWORD, DWORD)

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
  if(reason == DLL_PROCESS_ATTACH)
  {
    ProxyLog("VERSION.dll proxy loaded");

    g_real = LoadLibraryW(L"version_real.dll");

    // configure renderdoc before loading it - the core reads these in DllMain
    SetEnvironmentVariableA("RENDERDOC_CAPFILE", "D:\\git\\renderdoc-nikki\\nikki\\captures\\nikki");

    // Load rendertest.dll by ABSOLUTE path (same dir as this proxy), so we
    // never pick up a stale copy from the CWD/PATH search.
    HMODULE rd = NULL;
    {
      wchar_t proxyPath[MAX_PATH] = {0};
      GetModuleFileNameW(hModule, proxyPath, MAX_PATH - 1);
      wchar_t *slash = wcsrchr(proxyPath, L'\\');
      if(slash)
        wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - proxyPath), L"rendertest.dll");
      rd = LoadLibraryW(proxyPath);
      ProxyLog("rendertest.dll load attempt: %ls -> %p", proxyPath, (void *)rd);
    }
    if(rd)
    {
      ProxyLog("rendertest.dll loaded");

      typedef void(__cdecl *GetDefOptsFn)(void *);
      typedef void(__cdecl *SetOptsFn)(void *);
      typedef void(__cdecl *SetCapFileFn)(const char *);

      GetDefOptsFn gd = (GetDefOptsFn)GetProcAddress(rd, "RENDERTEST_GetDefaultCaptureOptions");
      SetOptsFn so = (SetOptsFn)GetProcAddress(rd, "INTERNAL_SetCaptureOptions");
      SetCapFileFn scf = (SetCapFileFn)GetProcAddress(rd, "INTERNAL_SetCaptureFile");

      if(gd && so)
      {
        char opts[32] = {0};
        gd(opts);
        // byte 13 of CaptureOptions = hookIntoChildren (see capture_options.h layout)
        opts[13] = 1;
        so(opts);
        if(scf)
          scf("D:\\git\\renderdoc-nikki\\nikki\\captures\\nikki");
        ProxyLog("capture options configured (hookIntoChildren=true)");
      }
      else
      {
        ProxyLog("could not resolve config functions from rendertest.dll");
      }
    }
    else
    {
      ProxyLog("rendertest.dll NOT loaded");
    }
  }
  return TRUE;
}
