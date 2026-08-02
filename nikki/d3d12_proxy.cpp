// d3d12.dll proxy for InfinityNikki - intercepts D3D12CreateDevice and wraps the
// device with the already-injected rendertest core (loaded via the VERSION.dll
// proxy + hookIntoChildren chain, so no LoadLibrary is needed here - this avoids
// ACE detection entirely).
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>
#include <stdarg.h>

#pragma comment(lib, "kernel32.lib")

#define LOG_FILE L"D:\\git\\rendertst-nikki\\nikki\\d3d12_proxy.log"

static void Log(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  char buf[1024];
  int len = vsprintf_s(buf, sizeof(buf), fmt, args);
  va_end(args);
  HANDLE f = CreateFileW(LOG_FILE, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if(f != INVALID_HANDLE_VALUE)
  {
    DWORD written;
    WriteFile(f, buf, (DWORD)len, &written, NULL);
    CloseHandle(f);
  }
}

static const char *g_lastFwd = "none";
static HMODULE g_hReal = NULL;
static FARPROC(WINAPI *g_realGetProcAddress)(HMODULE, LPCSTR) = NULL;
extern "C" IMAGE_DOS_HEADER __ImageBase;

typedef HRESULT(WINAPI *PFN_WrapD3D12Device)(void *, void **, REFIID);

static void SaveRealGetProcAddress()
{
  if(g_realGetProcAddress) return;
  HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
  if(kernel32)
    g_realGetProcAddress =
        (FARPROC(WINAPI *)(HMODULE, LPCSTR))::GetProcAddress(kernel32, "GetProcAddress");
}

template <typename T>
static inline T GetReal(const char *name)
{
  if(!g_hReal) return NULL;
  if(g_realGetProcAddress)
    return (T)g_realGetProcAddress(g_hReal, name);
  return (T)::GetProcAddress(g_hReal, name);
}

static void LoadRealD3D12()
{
  if(g_hReal) return;
  SaveRealGetProcAddress();
  // MUST load from System32 - loading a copy from the game dir returns
  // DXGI_ERROR_UNSUPPORTED (see previous findings)
  wchar_t path[MAX_PATH];
  GetSystemDirectoryW(path, MAX_PATH);
  wcscat_s(path, L"\\d3d12.dll");
  g_hReal = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if(g_hReal)
    Log("Real d3d12 loaded at %p\n", g_hReal);
  else
    Log("ERROR: System32 d3d12.dll load failed (err=%u)\n", GetLastError());
}

static void WrapDeviceWithRendertest(void *realDevice, REFIID riid, void **outDevice)
{
  *outDevice = NULL;
  // rendertest should already be in the process (via VERSION.dll proxy + hookIntoChildren)
  HMODULE rt = GetModuleHandleA("rendertest.dll");
  if(!rt)
  {
    Log("rendertest.dll NOT in process - trying to load from game dir\n");
    wchar_t myPath[MAX_PATH];
    GetModuleFileNameW((HMODULE)&__ImageBase, myPath, MAX_PATH);
    wchar_t *slash = wcsrchr(myPath, L'\\');
    if(slash)
      wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - myPath), L"rendertest.dll");
    rt = LoadLibraryW(myPath);
    if(!rt)
    {
      Log("rendertest.dll load failed err=%u\n", GetLastError());
      return;
    }
    Log("rendertest.dll loaded at %p (late)\n", rt);
  }

  PFN_WrapD3D12Device wrap = (PFN_WrapD3D12Device)GetProcAddress(rt, "RENDERTEST_WrapD3D12Device");
  if(!wrap)
  {
    Log("RENDERTEST_WrapD3D12Device not found!\n");
    return;
  }

  void *wrappedDev = NULL;
  HRESULT wrapHr = wrap(realDevice, &wrappedDev, riid);
  Log("RENDERTEST_WrapD3D12Device hr=0x%08X wrapped=%p\n", wrapHr, wrappedDev);
  if(SUCCEEDED(wrapHr) && wrappedDev)
    *outDevice = wrappedDev;
}

#define FWD(name) g_lastFwd = #name; LoadRealD3D12()

extern "C" HRESULT WINAPI _exp_D3D12GetDebugInterface(REFIID riid, void **ppvDebug)
{ FWD(D3D12GetDebugInterface);
  typedef HRESULT(WINAPI *PFN)(REFIID, void **);
  static PFN p = NULL; if(!p) p = GetReal<PFN>("D3D12GetDebugInterface");
  return p ? p(riid, ppvDebug) : E_FAIL;
}

extern "C" HRESULT WINAPI _exp_D3D12GetInterface(REFIID riid, void **ppv)
{ FWD(D3D12GetInterface);
  typedef HRESULT(WINAPI *PFN)(REFIID, void **);
  static PFN p = NULL; if(!p) p = GetReal<PFN>("D3D12GetInterface");
  return p ? p(riid, ppv) : E_FAIL;
}

extern "C" HRESULT WINAPI _exp_D3D12EnableExperimentalFeatures(
    UINT NumFeatures, const IID *pIIDs, void *pConfigurationStructs, UINT *pConfigurationStructSizes)
{ FWD(D3D12EnableExperimentalFeatures);
  typedef HRESULT(WINAPI *PFN)(UINT, const IID *, void *, UINT *);
  static PFN p = NULL; if(!p) p = GetReal<PFN>("D3D12EnableExperimentalFeatures");
  return p ? p(NumFeatures, pIIDs, pConfigurationStructs, pConfigurationStructSizes) : E_FAIL;
}

extern "C" HRESULT WINAPI _exp_D3D12SerializeRootSignature(
    const D3D12_ROOT_SIGNATURE_DESC *pRootSignature, D3D_ROOT_SIGNATURE_VERSION Version,
    ID3DBlob **ppBlob, ID3DBlob **ppErrorBlob)
{ FWD(D3D12SerializeRootSignature);
  typedef HRESULT(WINAPI *PFN)(const D3D12_ROOT_SIGNATURE_DESC *, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob **, ID3DBlob **);
  static PFN p = NULL; if(!p) p = GetReal<PFN>("D3D12SerializeRootSignature");
  return p ? p(pRootSignature, Version, ppBlob, ppErrorBlob) : E_FAIL;
}

extern "C" HRESULT WINAPI _exp_D3D12SerializeVersionedRootSignature(
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *pRootSignature,
    ID3DBlob **ppBlob, ID3DBlob **ppErrorBlob)
{ FWD(D3D12SerializeVersionedRootSignature);
  typedef HRESULT(WINAPI *PFN)(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *, ID3DBlob **, ID3DBlob **);
  static PFN p = NULL; if(!p) p = GetReal<PFN>("D3D12SerializeVersionedRootSignature");
  return p ? p(pRootSignature, ppBlob, ppErrorBlob) : E_FAIL;
}

extern "C" HRESULT WINAPI _exp_D3D12CreateRootSignatureDeserializer(
    LPCVOID pSrcData, SIZE_T SrcDataSizeInBytes,
    REFIID pRootSignatureDeserializerInterface, void **ppRootSignatureDeserializer)
{ FWD(D3D12CreateRootSignatureDeserializer);
  typedef HRESULT(WINAPI *PFN)(LPCVOID, SIZE_T, REFIID, void **);
  static PFN p = NULL; if(!p) p = GetReal<PFN>("D3D12CreateRootSignatureDeserializer");
  return p ? p(pSrcData, SrcDataSizeInBytes, pRootSignatureDeserializerInterface, ppRootSignatureDeserializer) : E_FAIL;
}

extern "C" HRESULT WINAPI _exp_D3D12CreateVersionedRootSignatureDeserializer(
    LPCVOID pSrcData, SIZE_T SrcDataSizeInBytes,
    REFIID pRootSignatureDeserializerInterface, void **ppRootSignatureDeserializer)
{ FWD(D3D12CreateVersionedRootSignatureDeserializer);
  typedef HRESULT(WINAPI *PFN)(LPCVOID, SIZE_T, REFIID, void **);
  static PFN p = NULL; if(!p) p = GetReal<PFN>("D3D12CreateVersionedRootSignatureDeserializer");
  return p ? p(pSrcData, SrcDataSizeInBytes, pRootSignatureDeserializerInterface, ppRootSignatureDeserializer) : E_FAIL;
}

typedef UINT D3D12CORE_LAYERED_DEVICE_VERSION;
struct D3D12CORE_LAYERED_DEVICE { void *pVtbl; UINT Version; };

extern "C" HRESULT WINAPI _exp_D3D12CoreCreateLayeredDevice(
    D3D12CORE_LAYERED_DEVICE *pLayeredDevice, REFIID riid, void **ppvDevice)
{ FWD(D3D12CoreCreateLayeredDevice);
  typedef HRESULT(WINAPI *PFN)(D3D12CORE_LAYERED_DEVICE *, REFIID, void **);
  static PFN p = NULL; if(!p) p = GetReal<PFN>("D3D12CoreCreateLayeredDevice");
  return p ? p(pLayeredDevice, riid, ppvDevice) : E_FAIL;
}

extern "C" SIZE_T WINAPI _exp_D3D12CoreGetLayeredDeviceSize(const void *unknown, UINT numArgs)
{ FWD(D3D12CoreGetLayeredDeviceSize);
  typedef SIZE_T(WINAPI *PFN)(const void *, UINT);
  static PFN p = NULL; if(!p) p = GetReal<PFN>("D3D12CoreGetLayeredDeviceSize");
  return p ? p(unknown, numArgs) : 0;
}

extern "C" D3D12CORE_LAYERED_DEVICE_VERSION WINAPI _exp_D3D12CoreRegisterLayers(
    D3D12CORE_LAYERED_DEVICE *pLayers, UINT numLayers)
{ FWD(D3D12CoreRegisterLayers);
  typedef D3D12CORE_LAYERED_DEVICE_VERSION(WINAPI *PFN)(D3D12CORE_LAYERED_DEVICE *, UINT);
  static PFN p = NULL; if(!p) p = GetReal<PFN>("D3D12CoreRegisterLayers");
  return p ? p(pLayers, numLayers) : 0;
}

extern "C" HRESULT WINAPI _exp_D3D12DeviceRemovedExtendedData(
    ID3D12Device *pDevice, D3D12_DRED_VERSION version, D3D12_DEVICE_REMOVED_EXTENDED_DATA *pData)
{ FWD(D3D12DeviceRemovedExtendedData);
  typedef HRESULT(WINAPI *PFN)(ID3D12Device *, D3D12_DRED_VERSION, D3D12_DEVICE_REMOVED_EXTENDED_DATA *);
  static PFN p = NULL; if(!p) p = GetReal<PFN>("D3D12DeviceRemovedExtendedData");
  return p ? p(pDevice, version, pData) : E_FAIL;
}

extern "C" HRESULT WINAPI _exp_SetAppCompatStringPointer(const char *name, const char *value)
{ FWD(SetAppCompatStringPointer);
  typedef HRESULT(WINAPI *PFN)(const char *, const char *);
  static PFN p = NULL; if(!p) p = GetReal<PFN>("SetAppCompatStringPointer");
  return p ? p(name, value) : E_FAIL;
}

extern "C" HRESULT WINAPI _exp_GetBehaviorValue(void *outValue, void *someParam)
{ FWD(GetBehaviorValue);
  typedef HRESULT(WINAPI *PFN)(void *, void *);
  static PFN p = NULL; if(!p) p = GetReal<PFN>("GetBehaviorValue");
  return p ? p(outValue, someParam) : E_FAIL;
}

extern "C" HRESULT WINAPI _exp_D3D12PIXEventsReplaceBlock(void *a, void *b, void *c, void *d)
{ FWD(D3D12PIXEventsReplaceBlock);
  typedef HRESULT(WINAPI *PFN)(void *, void *, void *, void *);
  static PFN p = NULL; if(!p) p = GetReal<PFN>("D3D12PIXEventsReplaceBlock");
  return p ? p(a, b, c, d) : E_FAIL;
}

extern "C" void WINAPI _exp_D3D12PIXGetThreadInfo(void *a)
{ FWD(D3D12PIXGetThreadInfo);
  typedef void(WINAPI *PFN)(void *);
  static PFN p = NULL; if(!p) p = GetReal<PFN>("D3D12PIXGetThreadInfo");
  if(p) p(a);
}

extern "C" HRESULT WINAPI _exp_D3D12PIXNotifyWakeFromFenceSignal(void *a, int b)
{ FWD(D3D12PIXNotifyWakeFromFenceSignal);
  typedef HRESULT(WINAPI *PFN)(void *, int);
  static PFN p = NULL; if(!p) p = GetReal<PFN>("D3D12PIXNotifyWakeFromFenceSignal");
  return p ? p(a, b) : E_FAIL;
}

extern "C" HRESULT WINAPI _exp_D3D12PIXReportCounter(void *a, void *b)
{ FWD(D3D12PIXReportCounter);
  typedef HRESULT(WINAPI *PFN)(void *, void *);
  static PFN p = NULL; if(!p) p = GetReal<PFN>("D3D12PIXReportCounter");
  return p ? p(a, b) : E_FAIL;
}

extern "C" HRESULT WINAPI _exp_Ordinal99(void)
{ FWD(Ordinal99);
  typedef HRESULT(WINAPI *PFN)(void);
  static PFN p = NULL; if(!p) p = (PFN)g_realGetProcAddress(g_hReal, MAKEINTRESOURCEA(99));
  return p ? p() : E_FAIL;
}

extern "C" HRESULT WINAPI D3D12CreateDevice(
    IUnknown *pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel,
    REFIID riid, void **ppDevice)
{
  FWD(D3D12CreateDevice);
  typedef HRESULT(WINAPI *PFN)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
  static PFN pCachedReal = NULL;

  if(!pCachedReal && g_hReal)
    pCachedReal = (PFN)g_realGetProcAddress(g_hReal, "D3D12CreateDevice");

  Log("D3D12CreateDevice: calling real\n");

  HRESULT hr = E_FAIL;
  if(pCachedReal)
    hr = pCachedReal(pAdapter, MinimumFeatureLevel, riid, ppDevice);

  Log("D3D12CreateDevice: hr=0x%08X dev=%p\n", hr, ppDevice ? *ppDevice : NULL);

  // diagnostic switch: if D:\git\rendertst-nikki\nikki\nikkiproxy_nowrap.txt exists, skip wrapping
  // (returns the real device directly) so we can bisect whether the wrapped
  // device path is what crashes the game.
  bool skipWrap = (GetFileAttributesA("D:\\git\\rendertst-nikki\\nikki\\nikkiproxy_nowrap.txt") != INVALID_FILE_ATTRIBUTES);

  if(SUCCEEDED(hr) && ppDevice && *ppDevice && !skipWrap)
  {
    Log("D3D12CreateDevice: wrapping device with rendertest...\n");
    void *wrapped = NULL;
    WrapDeviceWithRendertest(*ppDevice, riid, &wrapped);
    if(wrapped)
      *ppDevice = wrapped;
    Log("D3D12CreateDevice: returning to game\n");
  }
  else if(skipWrap)
  {
    Log("D3D12CreateDevice: SKIPPING WRAP (diagnostic switch)\n");
  }

  return hr;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
  (void)hModule; (void)lpReserved;
  if(reason == DLL_PROCESS_ATTACH)
  {
    DeleteFileW(LOG_FILE);
    Log("=== d3d12 proxy DllMain (pid %d) ===\n", (int)GetCurrentProcessId());
    SaveRealGetProcAddress();
    LoadRealD3D12();
    Log("=== d3d12 proxy DllMain done ===\n");
  }
  else if(reason == DLL_PROCESS_DETACH)
  {
    Log("=== DLL UNLOAD: last_fwd=%s ===\n", g_lastFwd);
  }
  return TRUE;
}
