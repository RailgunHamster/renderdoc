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

#include "core/core.h"
#include "hooks/hooks.h"
#include "hooks/inline_hooks.h"
#include "dxgi_wrapped.h"
#include <winternl.h>

extern "C" void InstallDXGIFactoryInlineHooks();

typedef HRESULT(WINAPI *PFN_CREATE_DXGI_FACTORY)(REFIID, void **);
typedef HRESULT(WINAPI *PFN_CREATE_DXGI_FACTORY2)(UINT, REFIID, void **);
typedef HRESULT(WINAPI *PFN_GET_DEBUG_INTERFACE)(REFIID, void **);
typedef HRESULT(WINAPI *PFN_GET_DEBUG_INTERFACE1)(UINT, REFIID, void **);

MIDL_INTERFACE("9F251514-9D4D-4902-9D60-18988AB7D4B5")
IDXGraphicsAnalysis : public IUnknown
{
  virtual void STDMETHODCALLTYPE BeginCapture() = 0;
  virtual void STDMETHODCALLTYPE EndCapture() = 0;
};

struct RenderTestAnalysis : IDXGraphicsAnalysis
{
  // IUnknown boilerplate
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) { return E_NOINTERFACE; }
  ULONG STDMETHODCALLTYPE AddRef()
  {
    InterlockedIncrement(&m_iRefcount);
    return m_iRefcount;
  }
  ULONG STDMETHODCALLTYPE Release() { return InterlockedDecrement(&m_iRefcount); }
  unsigned int m_iRefcount = 0;

  // IDXGraphicsAnalysis
  void STDMETHODCALLTYPE BeginCapture()
  {
    DeviceOwnedWindow devWnd;
    RenderTest::Inst().GetActiveWindow(devWnd);

    RenderTest::Inst().StartFrameCapture(devWnd);
  }

  void STDMETHODCALLTYPE EndCapture()
  {
    DeviceOwnedWindow devWnd;
    RenderTest::Inst().GetActiveWindow(devWnd);

    RenderTest::Inst().EndFrameCapture(devWnd);
  }
};

struct DummyDXGIInfoQueue : public IDXGIInfoQueue
{
public:
  // IUnknown boilerplate
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) { return E_NOINTERFACE; }
  ULONG STDMETHODCALLTYPE AddRef()
  {
    InterlockedIncrement(&m_iRefcount);
    return m_iRefcount;
  }
  ULONG STDMETHODCALLTYPE Release() { return InterlockedDecrement(&m_iRefcount); }
  unsigned int m_iRefcount = 0;
  // IDXGIInfoQueue
  virtual HRESULT STDMETHODCALLTYPE SetMessageCountLimit(DXGI_DEBUG_ID Producer,
                                                         UINT64 MessageCountLimit)
  {
    return S_OK;
  }

  virtual void STDMETHODCALLTYPE ClearStoredMessages(DXGI_DEBUG_ID Producer) { return; }
  virtual HRESULT STDMETHODCALLTYPE GetMessage(DXGI_DEBUG_ID Producer, UINT64 MessageIndex,
                                               _Out_writes_bytes_opt_(*pMessageByteLength)
                                                   DXGI_INFO_QUEUE_MESSAGE *pMessage,
                                               _Inout_ SIZE_T *pMessageByteLength)
  {
    return S_OK;
  }

  virtual UINT64 STDMETHODCALLTYPE GetNumStoredMessagesAllowedByRetrievalFilters(DXGI_DEBUG_ID Producer)
  {
    return 0;
  }

  virtual UINT64 STDMETHODCALLTYPE GetNumStoredMessages(DXGI_DEBUG_ID Producer) { return 0; }
  virtual UINT64 STDMETHODCALLTYPE GetNumMessagesDiscardedByMessageCountLimit(DXGI_DEBUG_ID Producer)
  {
    return 0;
  }

  virtual UINT64 STDMETHODCALLTYPE GetMessageCountLimit(DXGI_DEBUG_ID Producer) { return 0; }
  virtual UINT64 STDMETHODCALLTYPE GetNumMessagesAllowedByStorageFilter(DXGI_DEBUG_ID Producer)
  {
    return 0;
  }

  virtual UINT64 STDMETHODCALLTYPE GetNumMessagesDeniedByStorageFilter(DXGI_DEBUG_ID Producer)
  {
    return 0;
  }

  virtual HRESULT STDMETHODCALLTYPE AddStorageFilterEntries(DXGI_DEBUG_ID Producer,
                                                            DXGI_INFO_QUEUE_FILTER *pFilter)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE GetStorageFilter(DXGI_DEBUG_ID Producer,
                                                     _Out_writes_bytes_opt_(*pFilterByteLength)
                                                         DXGI_INFO_QUEUE_FILTER *pFilter,
                                                     _Inout_ SIZE_T *pFilterByteLength)
  {
    return S_OK;
  }

  virtual void STDMETHODCALLTYPE ClearStorageFilter(DXGI_DEBUG_ID Producer) { return; }
  virtual HRESULT STDMETHODCALLTYPE PushEmptyStorageFilter(DXGI_DEBUG_ID Producer) { return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE PushDenyAllStorageFilter(DXGI_DEBUG_ID Producer)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE PushCopyOfStorageFilter(DXGI_DEBUG_ID Producer) { return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE PushStorageFilter(DXGI_DEBUG_ID Producer,
                                                      DXGI_INFO_QUEUE_FILTER *pFilter)
  {
    return S_OK;
  }

  virtual void STDMETHODCALLTYPE PopStorageFilter(DXGI_DEBUG_ID Producer) { return; }
  virtual UINT STDMETHODCALLTYPE GetStorageFilterStackSize(DXGI_DEBUG_ID Producer) { return 0; }
  virtual HRESULT STDMETHODCALLTYPE AddRetrievalFilterEntries(DXGI_DEBUG_ID Producer,
                                                              DXGI_INFO_QUEUE_FILTER *pFilter)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE GetRetrievalFilter(DXGI_DEBUG_ID Producer,
                                                       _Out_writes_bytes_opt_(*pFilterByteLength)
                                                           DXGI_INFO_QUEUE_FILTER *pFilter,
                                                       _Inout_ SIZE_T *pFilterByteLength)
  {
    return S_OK;
  }

  virtual void STDMETHODCALLTYPE ClearRetrievalFilter(DXGI_DEBUG_ID Producer) { return; }
  virtual HRESULT STDMETHODCALLTYPE PushEmptyRetrievalFilter(DXGI_DEBUG_ID Producer)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE PushDenyAllRetrievalFilter(DXGI_DEBUG_ID Producer)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE PushCopyOfRetrievalFilter(DXGI_DEBUG_ID Producer)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE PushRetrievalFilter(DXGI_DEBUG_ID Producer,
                                                        DXGI_INFO_QUEUE_FILTER *pFilter)
  {
    return S_OK;
  }

  virtual void STDMETHODCALLTYPE PopRetrievalFilter(DXGI_DEBUG_ID Producer) { return; }
  virtual UINT STDMETHODCALLTYPE GetRetrievalFilterStackSize(DXGI_DEBUG_ID Producer) { return 0; }
  virtual HRESULT STDMETHODCALLTYPE AddMessage(DXGI_DEBUG_ID Producer,
                                               DXGI_INFO_QUEUE_MESSAGE_CATEGORY Category,
                                               DXGI_INFO_QUEUE_MESSAGE_SEVERITY Severity,
                                               DXGI_INFO_QUEUE_MESSAGE_ID ID, LPCSTR pDescription)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE AddApplicationMessage(DXGI_INFO_QUEUE_MESSAGE_SEVERITY Severity,
                                                          LPCSTR pDescription)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE SetBreakOnCategory(DXGI_DEBUG_ID Producer,
                                                       DXGI_INFO_QUEUE_MESSAGE_CATEGORY Category,
                                                       BOOL bEnable)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE SetBreakOnSeverity(DXGI_DEBUG_ID Producer,
                                                       DXGI_INFO_QUEUE_MESSAGE_SEVERITY Severity,
                                                       BOOL bEnable)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE SetBreakOnID(DXGI_DEBUG_ID Producer,
                                                 DXGI_INFO_QUEUE_MESSAGE_ID ID, BOOL bEnable)
  {
    return S_OK;
  }

  virtual BOOL STDMETHODCALLTYPE GetBreakOnCategory(DXGI_DEBUG_ID Producer,
                                                    DXGI_INFO_QUEUE_MESSAGE_CATEGORY Category)
  {
    return FALSE;
  }

  virtual BOOL STDMETHODCALLTYPE GetBreakOnSeverity(DXGI_DEBUG_ID Producer,
                                                    DXGI_INFO_QUEUE_MESSAGE_SEVERITY Severity)
  {
    return FALSE;
  }

  virtual BOOL STDMETHODCALLTYPE GetBreakOnID(DXGI_DEBUG_ID Producer, DXGI_INFO_QUEUE_MESSAGE_ID ID)
  {
    return FALSE;
  }

  virtual void STDMETHODCALLTYPE SetMuteDebugOutput(DXGI_DEBUG_ID Producer, BOOL bMute) { return; }
  virtual BOOL STDMETHODCALLTYPE GetMuteDebugOutput(DXGI_DEBUG_ID Producer) { return FALSE; }
};

class DXGIHook : LibraryHook
{
public:
  void RegisterHooks()
  {
    RDCLOG("Registering DXGI hooks");

    // file switch: disable DXGI hooking entirely. Wrapping the DXGI factory
    // breaks this game's D3D12 device initialisation (wrapped adapter is passed
    // to the real D3D12CreateDevice -> failure -> the game retries loading
    // d3d12 every ~20s forever).
    if(GetFileAttributesA("D:\\git\\rendertst-nikki\\nikki\\nikkiproxy_disable_dxgi_hooks.txt") !=
       INVALID_FILE_ATTRIBUTES)
    {
      RDCLOG("DXGI hooks disabled by file switch");
      return;
    }

    LibraryHooks::RegisterLibraryHook("dxgi.dll", NULL);

    CreateDXGIFactory.Register("dxgi.dll", "CreateDXGIFactory", CreateDXGIFactory_hook);
    CreateDXGIFactory1.Register("dxgi.dll", "CreateDXGIFactory1", CreateDXGIFactory1_hook);
    CreateDXGIFactory2.Register("dxgi.dll", "CreateDXGIFactory2", CreateDXGIFactory2_hook);
    GetDebugInterface.Register("dxgi.dll", "DXGIGetDebugInterface", DXGIGetDebugInterface_hook);
    GetDebugInterface1.Register("dxgi.dll", "DXGIGetDebugInterface1", DXGIGetDebugInterface1_hook);

    // dxgi factory inline hooking via restore-call-repatch (no trampolines:
    // trampolines crash in this game). Installed so that even if the game
    // obtains the factory entry points through GetProcAddress (not IAT) it
    // still ends up in our wrappers.
    InstallDXGIFactoryInlineHooks();
  }

public:
  static void *s_cfFunc;
  static uint8_t s_cfOrig[12];
  static void *s_cf1Func;
  static uint8_t s_cf1Orig[12];
  static void *s_cf2Func;
  static uint8_t s_cf2Orig[12];

  static HRESULT WINAPI CreateDXGIFactory_inline_hook(__in REFIID riid, __out void **ppFactory)
  {
    RestoreInlineHookBytes(s_cfFunc, s_cfOrig);
    // Call the real function directly: the HookedFunction's orig may be NULL
    // because the game delay-loads dxgi.dll after registration.
    typedef HRESULT(WINAPI *PFN_CDXGI)(REFIID, void **);
    HRESULT ret = ((PFN_CDXGI)s_cfFunc)(riid, ppFactory);
    if(SUCCEEDED(ret))
      RefCountDXGIObject::HandleWrap("CreateDXGIFactory", riid, ppFactory);
    PatchInlineHookBytes(s_cfFunc, (void *)&CreateDXGIFactory_inline_hook);
    return ret;
  }

  static HRESULT WINAPI CreateDXGIFactory1_inline_hook(__in REFIID riid, __out void **ppFactory)
  {
    RestoreInlineHookBytes(s_cf1Func, s_cf1Orig);
    typedef HRESULT(WINAPI *PFN_CDXGI1)(REFIID, void **);
    HRESULT ret = ((PFN_CDXGI1)s_cf1Func)(riid, ppFactory);
    if(SUCCEEDED(ret))
      RefCountDXGIObject::HandleWrap("CreateDXGIFactory1", riid, ppFactory);
    PatchInlineHookBytes(s_cf1Func, (void *)&CreateDXGIFactory1_inline_hook);
    return ret;
  }

  static HRESULT WINAPI CreateDXGIFactory2_inline_hook(
      __in UINT Flags, __in REFIID riid, __out void **ppFactory)
  {
    RestoreInlineHookBytes(s_cf2Func, s_cf2Orig);
    typedef HRESULT(WINAPI *PFN_CDXGI2)(UINT, REFIID, void **);
    HRESULT ret = ((PFN_CDXGI2)s_cf2Func)(Flags, riid, ppFactory);
    if(SUCCEEDED(ret))
      RefCountDXGIObject::HandleWrap("CreateDXGIFactory2", riid, ppFactory);
    PatchInlineHookBytes(s_cf2Func, (void *)&CreateDXGIFactory2_inline_hook);
    return ret;
  }

private:
  static DXGIHook dxgihooks;

  RenderTestAnalysis m_RenderTestAnalysis;
  DummyDXGIInfoQueue m_DummyInfoQueue;

  HookedFunction<PFN_CREATE_DXGI_FACTORY> CreateDXGIFactory;
  HookedFunction<PFN_CREATE_DXGI_FACTORY> CreateDXGIFactory1;
  HookedFunction<PFN_CREATE_DXGI_FACTORY2> CreateDXGIFactory2;
  HookedFunction<PFN_GET_DEBUG_INTERFACE> GetDebugInterface;
  HookedFunction<PFN_GET_DEBUG_INTERFACE1> GetDebugInterface1;

  static HRESULT WINAPI CreateDXGIFactory_hook(__in REFIID riid, __out void **ppFactory)
  {
    Heartbeat("CreateDXGIFactory hook");
    // d3d12 inline hooking DISABLED for now: patching d3d12.dll triggers the
    // game's anti-cheat periodic code-integrity scan, which freezes the game
    // for many seconds at a time. Disable via file switch
    // (nikki\nikkiproxy_disable_inline.txt) or rebuild without the call.
    bool disableInline =
        (GetFileAttributesA("D:\\git\\rendertst-nikki\\nikki\\nikkiproxy_disable_inline.txt") !=
         INVALID_FILE_ATTRIBUTES);
    extern void InstallD3D12InlineHooks();
    if(!disableInline && GetModuleHandleA("d3d12.dll"))
      InstallD3D12InlineHooks();

    {
      FILE *f = NULL;
      fopen_s(&f, "D:\\git\\rendertst-nikki\\nikki\\marker_dxgi_factory.txt", "a");
      if(f)
      {
        fprintf(f, "CreateDXGIFactory hook called in pid %d\n", (int)GetCurrentProcessId());
        fclose(f);
      }
    }
    if(ppFactory)
      *ppFactory = NULL;
    HRESULT ret = dxgihooks.CreateDXGIFactory()(riid, ppFactory);

    if(SUCCEEDED(ret))
      RefCountDXGIObject::HandleWrap("CreateDXGIFactory", riid, ppFactory);

    return ret;
  }

  static HRESULT WINAPI CreateDXGIFactory1_hook(__in REFIID riid, __out void **ppFactory)
  {
    if(ppFactory)
      *ppFactory = NULL;
    HRESULT ret = dxgihooks.CreateDXGIFactory1()(riid, ppFactory);

    if(SUCCEEDED(ret))
      RefCountDXGIObject::HandleWrap("CreateDXGIFactory1", riid, ppFactory);

    return ret;
  }

  static HRESULT WINAPI CreateDXGIFactory2_hook(UINT Flags, REFIID riid, void **ppFactory)
  {
    if(ppFactory)
      *ppFactory = NULL;
    HRESULT ret = dxgihooks.CreateDXGIFactory2()(Flags, riid, ppFactory);

    if(SUCCEEDED(ret))
      RefCountDXGIObject::HandleWrap("CreateDXGIFactory2", riid, ppFactory);

    return ret;
  }

  static HRESULT WINAPI DXGIGetDebugInterface_hook(REFIID riid, void **ppDebug)
  {
    if(ppDebug)
      *ppDebug = NULL;

    if(riid == __uuidof(IDXGraphicsAnalysis))
    {
      dxgihooks.m_RenderTestAnalysis.AddRef();
      if(ppDebug)
        *ppDebug = &dxgihooks.m_RenderTestAnalysis;
      return S_OK;
    }
    if(riid == __uuidof(IDXGIInfoQueue))
    {
      RDCWARN(
          "Returning a dummy IDXGIInfoQueue that does nothing. RenderTest takes control of the "
          "debug layer.");
      dxgihooks.m_DummyInfoQueue.AddRef();
      if(ppDebug)
        *ppDebug = &dxgihooks.m_DummyInfoQueue;
      return S_OK;
    }

    // IDXGIDebug and IDXGIDebug1 can come through here, but we don't need to wrap them.

    if(dxgihooks.GetDebugInterface())
      return dxgihooks.GetDebugInterface()(riid, ppDebug);
    else
      return E_NOINTERFACE;
  }

  static HRESULT WINAPI DXGIGetDebugInterface1_hook(UINT Flags, REFIID riid, void **ppDebug)
  {
    if(ppDebug)
      *ppDebug = NULL;

    if(riid == __uuidof(IDXGraphicsAnalysis))
    {
      dxgihooks.m_RenderTestAnalysis.AddRef();
      if(ppDebug)
        *ppDebug = &dxgihooks.m_RenderTestAnalysis;
      return S_OK;
    }
    if(riid == __uuidof(IDXGIInfoQueue))
    {
      RDCWARN(
          "Returning a dummy IDXGIInfoQueue that does nothing. RenderTest takes control of the "
          "debug layer.");
      dxgihooks.m_DummyInfoQueue.AddRef();
      if(ppDebug)
        *ppDebug = &dxgihooks.m_DummyInfoQueue;
      return S_OK;
    }

    // IDXGIDebug and IDXGIDebug1 can come through here, but we don't need to wrap them.

    if(dxgihooks.GetDebugInterface1())
      return dxgihooks.GetDebugInterface1()(Flags, riid, ppDebug);
    else
      return E_NOINTERFACE;
  }
};

DXGIHook DXGIHook::dxgihooks;

void *DXGIHook::s_cfFunc = NULL;
uint8_t DXGIHook::s_cfOrig[12];
void *DXGIHook::s_cf1Func = NULL;
uint8_t DXGIHook::s_cf1Orig[12];
void *DXGIHook::s_cf2Func = NULL;
uint8_t DXGIHook::s_cf2Orig[12];

// exported so the core can install the DXGI factory inline hooks very early
// (from DllMain), before the game creates its factory.
extern "C" void InstallDXGIFactoryInlineHooks()
{
  static bool installed = false;
  if(installed)
    return;

  // dxgi.dll may not be loaded yet (the game delay-loads it); if so, retry
  // on the next call instead of giving up permanently.
  if(!GetModuleHandleA("dxgi.dll"))
    return;

  installed = true;

  HMODULE dxgi = GetModuleHandleA("dxgi.dll");
  DXGIHook::s_cfFunc = (void *)GetProcAddress(dxgi, "CreateDXGIFactory");
  DXGIHook::s_cf1Func = (void *)GetProcAddress(dxgi, "CreateDXGIFactory1");
  DXGIHook::s_cf2Func = (void *)GetProcAddress(dxgi, "CreateDXGIFactory2");
  if(DXGIHook::s_cfFunc)
  {
    SaveInlineHookBytes(DXGIHook::s_cfFunc, DXGIHook::s_cfOrig);
    PatchInlineHookBytes(DXGIHook::s_cfFunc, (void *)&DXGIHook::CreateDXGIFactory_inline_hook);
  }
  if(DXGIHook::s_cf1Func)
  {
    SaveInlineHookBytes(DXGIHook::s_cf1Func, DXGIHook::s_cf1Orig);
    PatchInlineHookBytes(DXGIHook::s_cf1Func, (void *)&DXGIHook::CreateDXGIFactory1_inline_hook);
  }
  if(DXGIHook::s_cf2Func)
  {
    SaveInlineHookBytes(DXGIHook::s_cf2Func, DXGIHook::s_cf2Orig);
    PatchInlineHookBytes(DXGIHook::s_cf2Func, (void *)&DXGIHook::CreateDXGIFactory2_inline_hook);
  }
  {
    FILE *f = NULL;
    fopen_s(&f, "D:\\git\\rendertst-nikki\\nikki\\marker_inlinehook.txt", "a");
    if(f)
    {
      fprintf(f, "dxgi inline hooks installed in pid %d (cf=%d cf1=%d cf2=%d)\n",
              (int)GetCurrentProcessId(), DXGIHook::s_cfFunc != NULL, DXGIHook::s_cf1Func != NULL,
              DXGIHook::s_cf2Func != NULL);
      fclose(f);
    }
  }
}
