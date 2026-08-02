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

// must be separate so that it's included first and not sorted by clang-format
#include <windows.h>

#include <Psapi.h>
#include <tchar.h>
#include <tlhelp32.h>
#include "common/formatting.h"
#include "core/core.h"
#include "os/os_specific.h"
#include "strings/string_utils.h"

#include <string>

static rdcarray<EnvironmentModification> &GetEnvModifications()
{
  static rdcarray<EnvironmentModification> envCallbacks;
  return envCallbacks;
}

struct InsensitiveComparison
{
  bool operator()(const rdcstr &a, const rdcstr &b) const { return strlower(a) < strlower(b); }
};

typedef std::map<rdcstr, rdcstr, InsensitiveComparison> EnvMap;

static EnvMap EnvStringToEnvMap(const wchar_t *envstring)
{
  EnvMap ret;

  const wchar_t *e = envstring;

  while(*e)
  {
    const wchar_t *equals = wcschr(e, L'=');

    rdcstr name = StringFormat::Wide2UTF8(rdcwstr(e, equals - e));
    rdcstr value = StringFormat::Wide2UTF8(equals + 1);

    ret[name] = value;

    // jump to \0 and past it
    e += wcslen(e) + 1;
  }

  return ret;
}

void Process::RegisterEnvironmentModification(const EnvironmentModification &modif)
{
  GetEnvModifications().push_back(modif);
}

static void ApplyEnvModifications(EnvMap &envValues,
                                  const rdcarray<EnvironmentModification> &modifications,
                                  bool setToSystem)
{
  for(size_t i = 0; i < modifications.size(); i++)
  {
    const EnvironmentModification &m = modifications[i];

    rdcstr value;

    auto it = envValues.find(m.name);
    if(it != envValues.end())
      value = it->second;

    switch(m.mod)
    {
      case EnvMod::Set: value = m.value.c_str(); break;
      case EnvMod::Append:
      {
        if(!value.empty())
        {
          if(m.sep == EnvSep::Platform || m.sep == EnvSep::SemiColon)
            value += ";";
          else if(m.sep == EnvSep::Colon)
            value += ":";
        }
        value += m.value.c_str();
        break;
      }
      case EnvMod::Prepend:
      {
        if(!value.empty())
        {
          rdcstr prep = m.value;
          if(m.sep == EnvSep::Platform || m.sep == EnvSep::SemiColon)
            prep += ";";
          else if(m.sep == EnvSep::Colon)
            prep += ":";
          value = prep + value;
        }
        else
        {
          value = m.value.c_str();
        }
        break;
      }
    }

    envValues[m.name] = value;

    if(setToSystem)
      SetEnvironmentVariableW(StringFormat::UTF82Wide(m.name).c_str(),
                              StringFormat::UTF82Wide(value).c_str());
  }
}

// on windows we apply environment changes here, after process initialisation
// but before any real work (in RenderTest::Initialise) so that we support
// injecting the dll into processes we didn't launch (ie didn't control the
// starting environment for), or even the application loading the dll itself
// without any interaction with our replay app.
void Process::ApplyEnvironmentModification()
{
  // turn environment string to a UTF-8 map
  LPWCH envStrings = GetEnvironmentStringsW();
  EnvMap envValues = EnvStringToEnvMap(envStrings);
  FreeEnvironmentStringsW(envStrings);
  rdcarray<EnvironmentModification> &modifications = GetEnvModifications();

  ApplyEnvModifications(envValues, modifications, true);

  // these have been applied to the current process
  modifications.clear();
}

rdcstr Process::GetEnvVariable(const rdcstr &name)
{
  DWORD len = GetEnvironmentVariableA(name.c_str(), NULL, 0);
  if(len == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND)
    return rdcstr();

  rdcstr ret;
  ret.resize(len + 1);

  GetEnvironmentVariableA(name.c_str(), ret.data(), len);
  ret.trim();
  return ret;
}

uint64_t Process::GetMemoryUsage()
{
  HANDLE proc = GetCurrentProcess();

  if(proc == NULL)
  {
    RDCERR("Couldn't open process: %d", GetLastError());
    return 0;
  }

  PROCESS_MEMORY_COUNTERS memInfo = {};

  uint64_t ret = 0;

  if(GetProcessMemoryInfo(proc, &memInfo, sizeof(memInfo)))
  {
    ret = memInfo.WorkingSetSize;
  }
  else
  {
    RDCERR("Couldn't get process memory info: %d", GetLastError());
  }

  return ret;
}

// helpers for various shims and dlls etc, not part of the public API
extern "C" __declspec(dllexport) void __cdecl INTERNAL_GetTargetControlIdent(uint32_t *ident)
{
  if(ident)
    *ident = RenderTest::Inst().GetTargetControlIdent();
}

extern "C" __declspec(dllexport) void __cdecl INTERNAL_SetCaptureOptions(CaptureOptions *opts)
{
  if(opts)
    RenderTest::Inst().SetCaptureOptions(*opts);
}

extern "C" __declspec(dllexport) void __cdecl INTERNAL_SetCaptureFile(const char *capfile)
{
  if(capfile)
    RenderTest::Inst().SetCaptureFileTemplate(capfile);
}

extern "C" __declspec(dllexport) void __cdecl INTERNAL_SetDebugLogFile(const char *logfile)
{
  RENDERTEST_SetDebugLogFile(logfile ? logfile : rdcstr());
}

static EnvironmentModification tempEnvMod;

extern "C" __declspec(dllexport) void __cdecl INTERNAL_EnvModName(const char *name)
{
  if(name)
    tempEnvMod.name = name;
}

extern "C" __declspec(dllexport) void __cdecl INTERNAL_EnvModValue(const char *value)
{
  if(value)
    tempEnvMod.value = value;
}

extern "C" __declspec(dllexport) void __cdecl INTERNAL_EnvSep(EnvSep *sep)
{
  if(sep)
    tempEnvMod.sep = *sep;
}

extern "C" __declspec(dllexport) void __cdecl INTERNAL_EnvMod(EnvMod *mod)
{
  if(mod)
  {
    tempEnvMod.mod = *mod;
    Process::RegisterEnvironmentModification(tempEnvMod);
  }
}

extern "C" __declspec(dllexport) void __cdecl INTERNAL_ApplyEnvMods(void *ignored)
{
  Process::ApplyEnvironmentModification();
}

// small helper to write debug output for the injection code paths, useful when
// debugging anti-cheat interactions on machines where the log file isn't available
static void InjectDebugLog(const char *fmt, ...)
{
  static CRITICAL_SECTION cs = {};
  static bool init = false;
  if(!init)
  {
    InitializeCriticalSection(&cs);
    init = true;
  }
  EnterCriticalSection(&cs);
  CreateDirectoryA("C:\\Users\\Administrator\\AppData\\Local\\Temp\\RenderTest", NULL);
  FILE *f = NULL;
  fopen_s(&f, "C:\\Users\\Administrator\\AppData\\Local\\Temp\\RenderTest\\inject_debug.log", "a");
  if(f)
  {
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
  }
  LeaveCriticalSection(&cs);
}

// Find a thread in the target process that is currently suspended (e.g. the main
// thread of a process created with CREATE_SUSPENDED), which we can safely hijack
// for SetThreadContext-based injection. Returns an open thread handle, or NULL.
static HANDLE FindSuspendedThread(DWORD pid)
{
  // The SetThreadContext hijack path is experimental - it is only used when
  // explicitly enabled, as the CreateRemoteThread path is more reliable.
  if(Process::GetEnvVariable("RENDERTEST_ENABLE_THREADHIJACK") == "")
    return NULL;

  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if(snap == INVALID_HANDLE_VALUE)
    return NULL;

  HANDLE ret = NULL;
  THREADENTRY32 te;
  RDCEraseEl(te);
  te.dwSize = sizeof(te);

  for(BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
  {
    if(te.th32OwnerProcessID != pid)
      continue;

    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT |
                              THREAD_QUERY_INFORMATION,
                          FALSE, te.th32ThreadID);
    if(h == NULL)
      continue;

    DWORD prev = SuspendThread(h);
    if(prev != (DWORD)-1)
    {
      // was already suspended before our probe -> candidate
      ResumeThread(h);
      if(prev > 0)
      {
        ret = h;
        break;
      }
    }
    CloseHandle(h);
  }

  CloseHandle(snap);
  return ret;
}

// CreateRemoteThread-based injection, used as a fallback when no suspended thread
// is available in the target process.
void InjectDLLRemoteThread(HANDLE hProcess, rdcwstr libName)
{
  wchar_t dllPath[MAX_PATH + 1] = {0};
  wcscpy_s(dllPath, libName.c_str());

  static HMODULE kernel32 = GetModuleHandleA("kernel32.dll");

  if(kernel32 == NULL)
  {
    RDCERR("Couldn't get handle for kernel32.dll");
    return;
  }

  void *remoteMem =
      VirtualAllocEx(hProcess, NULL, sizeof(dllPath), MEM_COMMIT, PAGE_EXECUTE_READWRITE);
  if(remoteMem)
  {
    BOOL success = WriteProcessMemory(hProcess, remoteMem, (void *)dllPath, sizeof(dllPath), NULL);
    if(success)
    {
      HANDLE hThread = CreateRemoteThread(
          hProcess, NULL, 1024 * 1024U,
          (LPTHREAD_START_ROUTINE)GetProcAddress(kernel32, "LoadLibraryW"), remoteMem, 0, NULL);
      if(hThread)
      {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
      }
      else
      {
        RDCERR("Couldn't create remote thread for LoadLibraryW: %u", GetLastError());
      }
    }
    else
    {
      RDCERR("Couldn't write remote memory %p with dllPath '%ls': %u", remoteMem, dllPath,
             GetLastError());
    }

    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
  }
  else
  {
    RDCERR("Couldn't allocate remote memory for DLL '%ls': %u", libName.c_str(), GetLastError());
  }
}

// CreateRemoteThread-based function call injection, used as a fallback for
// processes that are already running (no suspended thread available).

// Returns the remote address of a function in the injected module, given the local
// module base and the remote module base.
static uintptr_t RemoteFuncAddress(uintptr_t RENDERTEST_remote, const char *funcName)
{
  HMODULE RENDERTEST_local = GetModuleHandleA(STRINGIZE(RDOC_BASE_NAME) ".dll");
  uintptr_t func_local = (uintptr_t)GetProcAddress(RENDERTEST_local, funcName);
  if(func_local == 0)
    return 0;
  return func_local + RENDERTEST_remote - (uintptr_t)RENDERTEST_local;
}
void InjectFunctionCallRemoteThread(HANDLE hProcess, uintptr_t RENDERTEST_remote,
                                    const char *funcName, void *data, const size_t dataLen)
{
  if(dataLen == 0)
  {
    RDCERR("Invalid function call injection attempt");
    return;
  }

  RDCDEBUG("Injecting call to %s", funcName);

  uintptr_t func_remote = RemoteFuncAddress(RENDERTEST_remote, funcName);
  if(func_remote == 0)
  {
    RDCERR("Couldn't resolve remote function %s", funcName);
    return;
  }

  void *remoteMem = VirtualAllocEx(hProcess, NULL, dataLen, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
  SIZE_T numWritten;
  WriteProcessMemory(hProcess, remoteMem, data, dataLen, &numWritten);

  HANDLE hThread =
      CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)func_remote, remoteMem, 0, NULL);
  WaitForSingleObject(hThread, INFINITE);

  ReadProcessMemory(hProcess, remoteMem, data, dataLen, &numWritten);

  CloseHandle(hThread);
  VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
}

// SetThreadContext-based remote injection. Hijacks a suspended thread in the remote
// process (typically the main thread of a process created with CREATE_SUSPENDED) to
// run a full injection payload. This avoids CreateRemoteThread which is easily
// detected by anti-cheat systems such as CrashSight or ACE (NtCreateThreadEx
// monitoring).
//
// The stub is a three-phase payload:
//   Phase A: call ntdll!LdrInitializeThunk(LoadLibraryW, dllPath) - this runs the
//            full process/thread loader initialisation (LdrpInitializeProcess +
//            LdrpInitializeThread) on the hijacked thread, then loads the target DLL.
//            Without this, running a DllMain on a freshly created suspended process
//            crashes because the loader state is incomplete.
//   Phase B: after 'done' is observed by the injector, it fills the call table
//            (INTERNAL_* config calls with their argument buffers) and sets cfgGo.
//            The stub calls each entry: func(arg).
//   Phase C: after marker 5 is observed, the injector reads back results and sets
//            'go'. The stub restores the original stack pointer, zeroes the volatile
//            registers and jumps to the process entry point. It must NOT resume the
//            original context (which would re-run LdrpInitializeProcess and crash).
//
// Work block layout (rsi points here):
//   +0x00 resultPtr (qword)  -> result/done block:
//                              +0x00 result (dword), +0x04 done (dword),
//                              +0x08 go (dword),     +0x10 marker (dword)
//   +0x08 entry     (qword)  - original thread start parameter (process entry point)
//   +0x10 origRsp   (qword)  - original stack pointer
//   +0x18 callTable (qword)  - [count][pairs of (func, arg)] (1-based index)
//   +0x20 marker    (dword)
//   +0x24 cfgGo     (dword)
//   +0x28 go        (dword)
struct InjectConfigCall
{
  uintptr_t funcRemote;
  const void *data;
  size_t dataLen;
};

// offsets within the work block
enum
{
  WK_RESULT = 0x00,
  WK_ENTRY = 0x08,
  WK_ORIG_RSP = 0x10,
  WK_TABLE = 0x18,
  WK_MARKER = 0x20,
  WK_CFGGO = 0x24,
  WK_GO = 0x28,
};

// offsets within the result block (pointed to by work+0x00)
enum
{
  RS_RESULT = 0x00,
  RS_DONE = 0x04,
  RS_GO = 0x08,
  RS_MARKER = 0x10,
};

// layout of the remote stub allocation
enum
{
  STUB_STACK_SIZE = 0x20000,
  STUB_WORK_OFF = 0x400,
  STUB_TABLE_OFF = 0x800,
  STUB_PATH_OFF = 0x1000,
};

  // Phase A: set up the stub + work block, hijack the suspended thread and load the
  // DLL via LdrInitializeThunk. Returns the remote resultPtr on success (the stub
  // thread is left spinning at the cfgGo wait), or 0 on failure.
  //
  // Note: ntdll!LdrInitializeThunk terminates the thread after the start routine
  // returns, so we do NOT use it. The stub calls LoadLibraryW directly - the loader
  // handles a fresh process fine, as proven by the CreateRemoteThread+LoadLibraryW
  // path that RenderTest has always used.
  //
  // For debugging: if RENDERTEST_TEST_DLL is set, that DLL is loaded instead.
static uintptr_t InjectPayloadPhaseA(HANDLE hProcess, HANDLE hThread, const rdcwstr &dllPath)
{
  InjectDebugLog("InjectPayloadPhaseA: pid=%u", GetProcessId(hProcess));
  if(hProcess == NULL || hThread == NULL || dllPath.length() == 0)
  {
    InjectDebugLog("InjectPayloadPhaseA: bad params");
    return 0;
  }

  uint8_t stub[0xA4] = {0};

  // 0x00 mov rsp, imm64(stubStackTop)          ; use our own committed stack buffer
  stub[0x00] = 0x48; stub[0x01] = 0xBC;
  // stack top filled in below (at 0x02)
  // 0x0A mov rsi, imm64(work)
  stub[0x0A] = 0x48; stub[0x0B] = 0xBE;
  // work ptr filled in below (at 0x0C)
  // 0x14 mov dword [rsi+0x20], 1               ; marker = 1
  stub[0x14] = 0xC7; stub[0x15] = 0x46; stub[0x16] = WK_MARKER; stub[0x17] = 0x01;
  stub[0x18] = 0; stub[0x19] = 0; stub[0x1A] = 0;
  // 0x1B mov rax, imm64(LoadLibraryW)
  stub[0x1B] = 0x48; stub[0x1C] = 0xB8;
  // loadLibrary filled in below (at 0x1D)
  // 0x25 mov rcx, imm64(dllPath)
  stub[0x25] = 0x48; stub[0x26] = 0xB9;
  // dllPath filled in below (at 0x27)
  // 0x2F xor edx, edx
  stub[0x2F] = 0x31; stub[0x30] = 0xD2;
  // 0x31 call rax
  stub[0x31] = 0xFF; stub[0x32] = 0xD0;
  // 0x33 mov dword [rsi+0x20], 3               ; marker = 3
  stub[0x33] = 0xC7; stub[0x34] = 0x46; stub[0x35] = WK_MARKER; stub[0x36] = 0x03;
  stub[0x37] = 0; stub[0x38] = 0; stub[0x39] = 0;
  // 0x3A mov rcx, [rsi+0x00]                   ; rcx = resultPtr
  stub[0x3A] = 0x48; stub[0x3B] = 0x8B; stub[0x3C] = 0x4E; stub[0x3D] = WK_RESULT;
  // 0x3E mov [rcx], eax                        ; store LoadLibrary result
  stub[0x3E] = 0x89; stub[0x3F] = 0x01;
  // 0x40 mov dword [rcx+4], 1                  ; done = 1
  stub[0x40] = 0xC7; stub[0x41] = 0x41; stub[0x42] = RS_DONE; stub[0x43] = 0x01;
  stub[0x44] = 0; stub[0x45] = 0; stub[0x46] = 0;
  // 0x47 mov rdi, [rsi+0x18]                   ; rdi = call table
  stub[0x47] = 0x48; stub[0x48] = 0x8B; stub[0x49] = 0x7E; stub[0x4A] = WK_TABLE;
  // 0x4B mov ecx, [rdi]                        ; count
  stub[0x4B] = 0x8B; stub[0x4C] = 0x0F;
  // 0x4D test ecx, ecx
  stub[0x4D] = 0x85; stub[0x4E] = 0xC9;
  // 0x4F je 0x6C                               ; skip loop if count == 0
  stub[0x4F] = 0x74; stub[0x50] = 0x1B;
  // 0x51 loop: mov rax, [rdi+rcx*8]            ; func
  stub[0x51] = 0x48; stub[0x52] = 0x8B; stub[0x53] = 0x44; stub[0x54] = 0xCF; stub[0x55] = 0x00;
  // 0x56 mov rdx, [rdi+rcx*8+8]                ; arg
  stub[0x56] = 0x48; stub[0x57] = 0x8B; stub[0x58] = 0x54; stub[0x59] = 0xCF; stub[0x5A] = 0x08;
  // 0x5B sub rsp, 0x30
  stub[0x5B] = 0x48; stub[0x5C] = 0x83; stub[0x5D] = 0xEC; stub[0x5E] = 0x30;
  // 0x5F mov rcx, rdx
  stub[0x5F] = 0x48; stub[0x60] = 0x89; stub[0x61] = 0xD1;
  // 0x62 call rax
  stub[0x62] = 0xFF; stub[0x63] = 0xD0;
  // 0x64 add rsp, 0x30
  stub[0x64] = 0x48; stub[0x65] = 0x83; stub[0x66] = 0xC4; stub[0x67] = 0x30;
  // 0x68 dec ecx
  stub[0x68] = 0xFF; stub[0x69] = 0xC9;
  // 0x6A jnz 0x51
  stub[0x6A] = 0x75; stub[0x6B] = 0xE5;
  // 0x6C mov dword [rsi+0x20], 5               ; marker = 5
  stub[0x6C] = 0xC7; stub[0x6D] = 0x46; stub[0x6E] = WK_MARKER; stub[0x6F] = 0x05;
  stub[0x70] = 0; stub[0x71] = 0; stub[0x72] = 0;
  // 0x73 spin: mov eax, [rsi+0x24]             ; wait for cfgGo
  stub[0x73] = 0x8B; stub[0x74] = 0x46; stub[0x75] = WK_CFGGO;
  // 0x76 test eax, eax
  stub[0x76] = 0x85; stub[0x77] = 0xC0;
  // 0x78 jnz 0x7E
  stub[0x78] = 0x75; stub[0x79] = 0x04;
  // 0x7A pause
  stub[0x7A] = 0xF3; stub[0x7B] = 0x90;
  // 0x7C jmp 0x73
  stub[0x7C] = 0xEB; stub[0x7D] = 0xF5;
  // 0x7E mov rsp, [rsi+0x10]                   ; restore original stack
  stub[0x7E] = 0x48; stub[0x7F] = 0x8B; stub[0x80] = 0x66; stub[0x81] = WK_ORIG_RSP;
  // 0x82 xor eax, eax
  stub[0x82] = 0x31; stub[0x83] = 0xC0;
  // 0x84 xor ecx, ecx
  stub[0x84] = 0x31; stub[0x85] = 0xC9;
  // 0x86 xor edx, edx
  stub[0x86] = 0x31; stub[0x87] = 0xD2;
  // 0x88 xor r8d, r8d
  stub[0x88] = 0x45; stub[0x89] = 0x31; stub[0x8A] = 0xC0;
  // 0x8B xor r9d, r9d
  stub[0x8B] = 0x45; stub[0x8C] = 0x31; stub[0x8D] = 0xC9;
  // 0x8E xor r10d, r10d
  stub[0x8E] = 0x45; stub[0x8F] = 0x31; stub[0x90] = 0xD2;
  // 0x91 xor r11d, r11d
  stub[0x91] = 0x45; stub[0x92] = 0x31; stub[0x93] = 0xDB;
  // 0x94 mov rbx, [rsi+0x08]                   ; rbx = entry point
  stub[0x94] = 0x48; stub[0x95] = 0x8B; stub[0x96] = 0x5E; stub[0x97] = WK_ENTRY;
  // 0x98 jmp rbx
  stub[0x98] = 0xFF; stub[0x99] = 0xE3;

  void *remoteStub = VirtualAllocEx(hProcess, NULL, STUB_STACK_SIZE, MEM_COMMIT,
                                    PAGE_EXECUTE_READWRITE);
  if(remoteStub == NULL)
  {
    InjectDebugLog("InjectPayloadPhaseA: VirtualAllocEx failed err=%u", GetLastError());
    RDCERR("Couldn't allocate remote memory for injection");
    return false;
  }

  uintptr_t stubBase = (uintptr_t)remoteStub;
  uintptr_t workPtr = stubBase + STUB_WORK_OFF;
  uintptr_t resultPtr = workPtr + 0x40;
  uintptr_t stubStackTop = stubBase + STUB_STACK_SIZE - 8;

  InjectDebugLog("InjectPayloadPhaseA: stub=%p work=%p result=%p stack=%p", remoteStub,
                 (void *)workPtr, (void *)resultPtr, (void *)stubStackTop);

  static HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
  uintptr_t loadLibrary = (uintptr_t)GetProcAddress(kernel32, "LoadLibraryW");

  void *remotePath = (void *)(stubBase + STUB_PATH_OFF);
  WriteProcessMemory(hProcess, remotePath, dllPath.c_str(),
                     (wcslen(dllPath.c_str()) + 1) * sizeof(wchar_t), NULL);

  memcpy(&stub[0x02], &stubStackTop, sizeof(uintptr_t));
  memcpy(&stub[0x0C], &workPtr, sizeof(uintptr_t));
  memcpy(&stub[0x1D], &loadLibrary, sizeof(uintptr_t));
  memcpy(&stub[0x27], &remotePath, sizeof(uintptr_t));

  // write the work block
  uint8_t work[0x48] = {0};
  uintptr_t tablePtr = stubBase + STUB_TABLE_OFF;

  CONTEXT origCtx;
  RDCEraseEl(origCtx);
  origCtx.ContextFlags = CONTEXT_FULL;
  if(!GetThreadContext(hThread, &origCtx))
  {
    InjectDebugLog("InjectPayloadPhaseA: GetThreadContext failed err=%u", GetLastError());
    RDCERR("Couldn't get thread context for injection: %u", GetLastError());
    VirtualFreeEx(hProcess, remoteStub, 0, MEM_RELEASE);
    return 0;
  }
  InjectDebugLog("InjectPayloadPhaseA: orig RIP=%p RSP=%p RCX=%p", (void *)origCtx.Rip,
                 (void *)origCtx.Rsp, (void *)origCtx.Rcx);

  memcpy(&work[WK_RESULT], &resultPtr, sizeof(uintptr_t));
  memcpy(&work[WK_ENTRY], &origCtx.Rcx, sizeof(uintptr_t));
  memcpy(&work[WK_ORIG_RSP], &origCtx.Rsp, sizeof(uintptr_t));
  memcpy(&work[WK_TABLE], &tablePtr, sizeof(uintptr_t));

  WriteProcessMemory(hProcess, (void *)workPtr, work, sizeof(work), NULL);
  WriteProcessMemory(hProcess, remoteStub, stub, sizeof(stub), NULL);

  CONTEXT hijackCtx = origCtx;
  hijackCtx.Rip = stubBase;
  hijackCtx.ContextFlags = CONTEXT_FULL;
  if(!SetThreadContext(hThread, &hijackCtx))
  {
    InjectDebugLog("InjectPayloadPhaseA: SetThreadContext failed err=%u", GetLastError());
    RDCERR("Couldn't set thread context for injection: %u", GetLastError());
    VirtualFreeEx(hProcess, remoteStub, 0, MEM_RELEASE);
    return 0;
  }

  if(ResumeThread(hThread) == (DWORD)-1)
  {
    InjectDebugLog("InjectPayloadPhaseA: ResumeThread failed err=%u", GetLastError());
    RDCERR("Couldn't resume injected thread: %u", GetLastError());
    VirtualFreeEx(hProcess, remoteStub, 0, MEM_RELEASE);
    return 0;
  }

  // phase A: wait for the DLL to be loaded (done flag)
  bool completed = false;
  uint32_t ret = 0;
  uint32_t marker = 0;
  for(int i = 0; i < 1000; i++)
  {
    uint32_t done = 0;
    uint32_t mark = 0;
    if(ReadProcessMemory(hProcess, (void *)(resultPtr + RS_DONE), &done, sizeof(done), NULL) &&
       done == 1)
    {
      ReadProcessMemory(hProcess, (void *)(resultPtr + RS_RESULT), &ret, sizeof(ret), NULL);
      completed = true;
      break;
    }
    ReadProcessMemory(hProcess, (void *)((resultPtr - 0x40) + WK_MARKER), &mark, sizeof(mark), NULL);
    if(marker != mark)
    {
      marker = mark;
      InjectDebugLog("InjectPayloadPhaseA: marker=%u", marker);
    }
    Threading::Sleep(10);
  }

  if(!completed)
  {
    InjectDebugLog("InjectPayloadPhaseA: TIMEOUT waiting for DLL load lastErr=%u marker=%u",
                   GetLastError(), marker);
    RDCERR("Timed out waiting for injected DLL to load");
    VirtualFreeEx(hProcess, remoteStub, 0, MEM_RELEASE);
    return 0;
  }

  InjectDebugLog("InjectPayloadPhaseA: DLL loaded, hmod=0x%x", ret);

  // keep the stub allocation alive - phase B/C uses the work block. The caller
  // frees it via InjectPayloadPhaseBC.
  return resultPtr;
}

// Phase B/C: write the config call table into the work block, let the stub run the
// calls, read back the ident, then let the stub continue to the process entry point.
// The stub allocation is freed at the end.
static bool InjectPayloadPhaseBC(HANDLE hProcess, uintptr_t resultPtr,
                                 const rdcarray<InjectConfigCall> &configCalls, void *identRet,
                                 size_t identLen)
{
  if(hProcess == NULL || resultPtr == 0)
    return false;

  uintptr_t workPtr = resultPtr - 0x40;
  uintptr_t remoteStub = workPtr - 0x400;
  uintptr_t stubBase = remoteStub;

  // write the call table (config calls) and let the stub run them.
  // allocate one remote buffer holding all argument blobs
  size_t totalArgLen = 0;
  for(int i = 0; i < configCalls.count(); i++)
    totalArgLen += (configCalls[i].dataLen + 7) & ~(size_t)7;

  void *remoteArg = NULL;
  if(totalArgLen > 0)
  {
    remoteArg = VirtualAllocEx(hProcess, NULL, totalArgLen, MEM_COMMIT, PAGE_READWRITE);
    if(remoteArg == NULL)
    {
      InjectDebugLog("InjectPayloadPhaseBC: arg alloc failed err=%u", GetLastError());
      VirtualFreeEx(hProcess, (void *)remoteStub, 0, MEM_RELEASE);
      return false;
    }
  }

  size_t tableLen = 8 + (size_t)configCalls.count() * 16;
  uint8_t *table = new uint8_t[tableLen];
  memset(table, 0, tableLen);
  uint32_t count = (uint32_t)configCalls.count();
  memcpy(table, &count, sizeof(uint32_t));

  size_t argOff = 0;
  uintptr_t identRemote = 0;
  for(int i = 0; i < configCalls.count(); i++)
  {
    uintptr_t argPtr = (uintptr_t)remoteArg + argOff;
    if(configCalls[i].data && configCalls[i].dataLen > 0)
      WriteProcessMemory(hProcess, (void *)argPtr, configCalls[i].data, configCalls[i].dataLen,
                         NULL);
    memcpy(table + 8 + (size_t)(i + 1) * 16 + 0, &configCalls[i].funcRemote, sizeof(uintptr_t));
    memcpy(table + 8 + (size_t)(i + 1) * 16 + 8, &argPtr, sizeof(uintptr_t));
    if(i == configCalls.count() - 1)
      identRemote = argPtr;
    argOff += (configCalls[i].dataLen + 7) & ~(size_t)7;
  }
  WriteProcessMemory(hProcess, (void *)(stubBase + STUB_TABLE_OFF), table, tableLen, NULL);
  delete[] table;

  uint32_t one = 1;
  WriteProcessMemory(hProcess, (void *)(workPtr + WK_CFGGO), &one, sizeof(one), NULL);

  // wait for marker 5 (config calls done)
  bool configDone = false;
  for(int i = 0; i < 1000; i++)
  {
    uint32_t mark = 0;
    if(ReadProcessMemory(hProcess, (void *)((resultPtr - 0x40) + WK_MARKER), &mark, sizeof(mark), NULL) &&
       mark == 5)
    {
      configDone = true;
      break;
    }
    Threading::Sleep(10);
  }

  if(!configDone)
  {
    InjectDebugLog("InjectPayloadPhaseBC: TIMEOUT waiting for config calls");
    RDCERR("Timed out waiting for injected config calls to complete");
  }
  else
  {
    if(identRet && identLen > 0 && identRemote)
      ReadProcessMemory(hProcess, (void *)identRemote, identRet, identLen, NULL);
  }

  // phase C: let the stub continue to the entry point
  WriteProcessMemory(hProcess, (void *)(workPtr + WK_GO), &one, sizeof(one), NULL);

  Threading::Sleep(50);

  if(remoteArg)
    VirtualFreeEx(hProcess, remoteArg, 0, MEM_RELEASE);
  VirtualFreeEx(hProcess, (void *)remoteStub, 0, MEM_RELEASE);

  return configDone;
}

uintptr_t FindRemoteDLL(DWORD pid, rdcstr libName)
{
  HANDLE hModuleSnap = INVALID_HANDLE_VALUE;

  rdcwstr wlibName = StringFormat::UTF82Wide(strlower(libName));

  // up to 10 retries
  for(int i = 0; i < 10; i++)
  {
    hModuleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);

    if(hModuleSnap == INVALID_HANDLE_VALUE)
    {
      DWORD err = GetLastError();

      RDCWARN("CreateToolhelp32Snapshot(%u) -> 0x%08x", pid, err);

      // retry if error is ERROR_BAD_LENGTH
      if(err == ERROR_BAD_LENGTH)
        continue;
    }

    // didn't retry, or succeeded
    break;
  }

  if(hModuleSnap == INVALID_HANDLE_VALUE)
  {
    RDCERR("Couldn't create toolhelp dump of modules in process %u", pid);
    return 0;
  }

  MODULEENTRY32 me32;
  RDCEraseEl(me32);
  me32.dwSize = sizeof(MODULEENTRY32);

  BOOL success = Module32First(hModuleSnap, &me32);

  if(success == FALSE)
  {
    DWORD err = GetLastError();

    RDCERR("Couldn't get first module in process %u: 0x%08x", pid, err);
    CloseHandle(hModuleSnap);
    return 0;
  }

  uintptr_t ret = 0;

  int numModules = 0;

  do
  {
    wchar_t modnameLower[MAX_MODULE_NAME32 + 1];
    RDCEraseEl(modnameLower);
    wcsncpy_s(modnameLower, me32.szModule, MAX_MODULE_NAME32);

    wchar_t *wc = &modnameLower[0];
    while(*wc)
    {
      *wc = towlower(*wc);
      wc++;
    }

    numModules++;

    if(wcsstr(modnameLower, wlibName.c_str()) == modnameLower)
    {
      ret = (uintptr_t)me32.modBaseAddr;
    }
  } while(ret == 0 && Module32Next(hModuleSnap, &me32));

  if(ret == 0)
  {
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);

    DWORD exitCode = 0;

    if(h)
      GetExitCodeProcess(h, &exitCode);

    if(h == NULL || exitCode != STILL_ACTIVE)
    {
      RDCERR(
          "Error injecting into remote process with PID %u which is no longer available.\n"
          "Possibly the process has crashed during early startup, or is missing DLLs to run?",
          pid);
    }
    else
    {
      RDCERR("Couldn't find module '%s' among %d modules", libName.c_str(), numModules);
    }

    if(h)
      CloseHandle(h);
  }

  CloseHandle(hModuleSnap);

  return ret;
}


// Creates a config call entry for the given INTERNAL_* function.
static void AddConfigCall(rdcarray<InjectConfigCall> &calls, uintptr_t RENDERTEST_remote,
                          const char *funcName, const void *data, size_t dataLen)
{
  InjectConfigCall call;
  call.funcRemote = RemoteFuncAddress(RENDERTEST_remote, funcName);
  call.data = data;
  call.dataLen = dataLen;
  calls.push_back(call);
}

static PROCESS_INFORMATION RunProcess(const rdcstr &app, const rdcstr &workingDir,
                                      const rdcstr &cmdLine,
                                      const rdcarray<EnvironmentModification> &env, bool internal,
                                      HANDLE *phChildStdOutput_Rd, HANDLE *phChildStdError_Rd)
{
  PROCESS_INFORMATION pi;
  STARTUPINFO si;
  SECURITY_ATTRIBUTES pSec;
  SECURITY_ATTRIBUTES tSec;

  RDCEraseEl(pi);
  RDCEraseEl(si);
  RDCEraseEl(pSec);
  RDCEraseEl(tSec);

  si.cb = sizeof(si);

  pSec.nLength = sizeof(pSec);
  tSec.nLength = sizeof(tSec);

  rdcwstr workdir = L"";

  if(!workingDir.empty())
    workdir = StringFormat::UTF82Wide(workingDir);
  else
    workdir = StringFormat::UTF82Wide(get_dirname(app));

  wchar_t *paramsAlloc = NULL;

  rdcwstr wapp = StringFormat::UTF82Wide(app);

  // CreateProcessW can modify the params, need space.
  size_t len = wapp.length() + 10;

  rdcwstr wcmd = L"";

  if(!cmdLine.empty())
  {
    wcmd = StringFormat::UTF82Wide(cmdLine);
    len += wcmd.length();
  }

  paramsAlloc = new wchar_t[len];

  RDCEraseMem(paramsAlloc, len * sizeof(wchar_t));

  wcscpy_s(paramsAlloc, len, L"\"");
  wcscat_s(paramsAlloc, len, wapp.c_str());
  wcscat_s(paramsAlloc, len, L"\"");

  if(!cmdLine.empty())
  {
    wcscat_s(paramsAlloc, len, L" ");
    wcscat_s(paramsAlloc, len, wcmd.c_str());
  }

  bool inheritHandles = false;

  HANDLE hChildStdOutput_Wr = 0, hChildStdError_Wr = 0;
  if(phChildStdOutput_Rd)
  {
    RDCASSERT(phChildStdError_Rd);

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if(!CreatePipe(phChildStdOutput_Rd, &hChildStdOutput_Wr, &sa, 0))
      RDCERR("Could not create pipe to read stdout");
    if(!SetHandleInformation(*phChildStdOutput_Rd, HANDLE_FLAG_INHERIT, 0))
      RDCERR("Could not set pipe handle information");

    if(!CreatePipe(phChildStdError_Rd, &hChildStdError_Wr, &sa, 0))
      RDCERR("Could not create pipe to read stdout");
    if(!SetHandleInformation(*phChildStdError_Rd, HANDLE_FLAG_INHERIT, 0))
      RDCERR("Could not set pipe handle information");

    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = hChildStdOutput_Wr;
    si.hStdError = hChildStdError_Wr;

    // Need to inherit handles in CreateProcess for ReadFile to read stdout
    inheritHandles = true;
  }

  // if it's a utility launch, hide the command prompt window from showing
  if(phChildStdOutput_Rd || internal)
    si.dwFlags |= STARTF_USESHOWWINDOW;

  if(!internal)
    RDCLOG("Running process %s", app.c_str());

  // turn environment string to a UTF-8 map
  std::wstring envString;

  if(!env.empty())
  {
    LPWCH envStrings = GetEnvironmentStringsW();
    EnvMap envValues = EnvStringToEnvMap(envStrings);
    FreeEnvironmentStringsW(envStrings);

    ApplyEnvModifications(envValues, env, false);

    for(auto it = envValues.begin(); it != envValues.end(); ++it)
    {
      envString += StringFormat::UTF82Wide(it->first).c_str();
      envString += L"=";
      envString += StringFormat::UTF82Wide(it->second).c_str();
      envString.push_back(0);
    }
  }

  BOOL retValue = CreateProcessW(
      NULL, paramsAlloc, &pSec, &tSec, inheritHandles, CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
      envString.empty() ? NULL : (void *)envString.data(), workdir.c_str(), &si, &pi);

  DWORD err = GetLastError();

  if(phChildStdOutput_Rd)
  {
    CloseHandle(hChildStdOutput_Wr);
    CloseHandle(hChildStdError_Wr);
  }

  SAFE_DELETE_ARRAY(paramsAlloc);

  if(!retValue)
  {
    if(!internal)
      RDCWARN("Process %s could not be loaded (error %d).", app.c_str(), err);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    RDCEraseEl(pi);
  }

  return pi;
}

rdcpair<RDResult, uint32_t> Process::InjectIntoProcess(uint32_t pid,
                                                       const rdcarray<EnvironmentModification> &env,
                                                       const rdcstr &capturefile,
                                                       const CaptureOptions &opts, bool waitForExit)
{
  rdcwstr wcapturefile = StringFormat::UTF82Wide(capturefile);

  HANDLE hProcess =
      OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                      PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE,
                  FALSE, pid);

  if(opts.delayForDebugger > 0)
  {
    RDCDEBUG("Waiting for debugger attach to %lu", pid);
    uint32_t timeout = 0;

    BOOL debuggerAttached = FALSE;

    while(!debuggerAttached)
    {
      CheckRemoteDebuggerPresent(hProcess, &debuggerAttached);

      Sleep(10);
      timeout += 10;

      if(timeout > opts.delayForDebugger * 1000)
        break;
    }

    if(debuggerAttached)
      RDCDEBUG("Debugger attach detected after %.2f s", float(timeout) / 1000.0f);
    else
      RDCDEBUG("Timed out waiting for debugger, gave up after %u s", opts.delayForDebugger);
  }

  RDCLOG("Injecting RenderTest into process %lu", pid);

  wchar_t RenderTestPath[MAX_PATH] = {0};
  GetModuleFileNameW(GetModuleHandleA(STRINGIZE(RDOC_BASE_NAME) ".dll"), &RenderTestPath[0],
                                      MAX_PATH - 1);

  wchar_t RenderTestPathLower[MAX_PATH] = {0};
  memcpy(RenderTestPathLower, RenderTestPath, MAX_PATH * sizeof(wchar_t));
  for(size_t i = 0; i < MAX_PATH && RenderTestPathLower[i]; i++)
  {
    // lowercase
    if(RenderTestPathLower[i] >= 'A' && RenderTestPathLower[i] <= 'Z')
      RenderTestPathLower[i] = 'a' + char(RenderTestPathLower[i] - 'A');

    // normalise paths
    if(RenderTestPathLower[i] == '/')
      RenderTestPathLower[i] = '\\';
  }

  BOOL isWow64 = FALSE;
  BOOL success = IsWow64Process(hProcess, &isWow64);

  if(!success)
  {
    DWORD err = GetLastError();
    RDResult result;
    SET_ERROR_RESULT(result, ResultCode::IncompatibleProcess,
                     "Couldn't determine bitness of process, err: %08x", err);
    CloseHandle(hProcess);
    return {result, 0};
  }

  bool capalt = false;

#if DISABLED(RDOC_X64)
  BOOL selfWow64 = FALSE;

  HANDLE hSelfProcess = GetCurrentProcess();

  // check to see if we're a WoW64 process
  success = IsWow64Process(hSelfProcess, &selfWow64);

  CloseHandle(hSelfProcess);

  if(!success)
  {
    DWORD err = GetLastError();
    RDResult result;
    SET_ERROR_RESULT(result, ResultCode::IncompatibleProcess,
                     "Couldn't determine bitness of self, err: %08x", err);
    CloseHandle(hProcess);
    return {result, 0};
  }

  // we know we're 32-bit, so if the target process is not wow64
  // and we are, it's 64-bit. If we're both not wow64 then we're
  // running on 32-bit windows, and if we're both wow64 then we're
  // both 32-bit on 64-bit windows.
  //
  // We don't support capturing 64-bit programs from a 32-bit install
  // because it's pointless - a 64-bit install will work for all in
  // that case. But we do want to handle the case of:
  // 64-bit RenderTest -> 32-bit program (via 32-bit RenderTestcmd)
  //    -> 64-bit program (going back to 64-bit RenderTestcmd).
  // so we try to see if we're an x86 invoked RenderTestcmd in an
  // otherwise 64-bit install, and 'promote' back to 64-bit.
  if(selfWow64 && !isWow64)
  {
    wchar_t *slash = wcsrchr(RenderTestPath, L'\\');

    if(slash && slash > RenderTestPath + 4)
    {
      slash -= 4;

      if(slash && !wcsncmp(slash, L"\\x86", 4))
      {
        RDCDEBUG("Promoting back to 64-bit");
        capalt = true;
      }
    }

    // if it looks like we're in the development environment, look for the alternate bitness in the
    // corresponding folder
    if(!capalt)
    {
      const wchar_t *devLocation = wcsstr(RenderTestPathLower, L"\\win32\\development\\");
      if(!devLocation)
        devLocation = wcsstr(RenderTestPathLower, L"\\win32\\release\\");

      if(devLocation)
      {
        RDCDEBUG("Promoting back to 64-bit");
        capalt = true;
      }
    }

    // if we couldn't promote, then bail out.
    if(!capalt)
    {
      RDCDEBUG("Running from %ls", RenderTestPathLower);

      CloseHandle(hProcess);
      RDResult result;
      SET_ERROR_RESULT(result, ResultCode::IncompatibleProcess,
                       "Can't capture 64-bit program with 32-bit build of RenderTest. Please run a "
                       "64-bit build of RenderTest");
      return {result, 0};
    }
  }
#else
  // farm off to alternate bitness RenderTestcmd.exe

  // if the target process is 'wow64' that means it's 32-bit.
  capalt = (isWow64 == TRUE);
#endif

  if(capalt)
  {
#if ENABLED(RDOC_X64)
    // if it looks like we're in the development environment, look for the alternate bitness in the
    // corresponding folder
    const wchar_t *devLocation = wcsstr(RenderTestPathLower, L"\\x64\\development\\");
    if(devLocation)
    {
      size_t idx = devLocation - RenderTestPathLower;

      RenderTestPath[idx] = 0;

      wcscat_s(RenderTestPath, L"\\Win32\\Development\\RenderTestcmd.exe");
    }

    if(!devLocation)
    {
      devLocation = wcsstr(RenderTestPathLower, L"\\x64\\release\\");

      if(devLocation)
      {
        size_t idx = devLocation - RenderTestPathLower;

        RenderTestPath[idx] = 0;

        wcscat_s(RenderTestPath, L"\\Win32\\Release\\RenderTestcmd.exe");
      }
    }

    if(!devLocation)
    {
      // look in a subfolder for x86.

      // remove the filename from the path
      wchar_t *slash = wcsrchr(RenderTestPath, L'\\');

      if(slash)
        *slash = 0;

      // append path
      wcscat_s(RenderTestPath, L"\\x86\\RenderTestcmd.exe");
    }
#else
    // if it looks like we're in the development environment, look for the alternate bitness in the
    // corresponding folder
    const wchar_t *devLocation = wcsstr(RenderTestPathLower, L"\\win32\\development\\");
    if(devLocation)
    {
      size_t idx = devLocation - RenderTestPathLower;

      RenderTestPath[idx] = 0;

      wcscat_s(RenderTestPath, L"\\x64\\Development\\RenderTestcmd.exe");
    }

    if(!devLocation)
    {
      devLocation = wcsstr(RenderTestPathLower, L"\\win32\\release\\");

      if(devLocation)
      {
        size_t idx = devLocation - RenderTestPathLower;

        RenderTestPath[idx] = 0;

        wcscat_s(RenderTestPath, L"\\x64\\Release\\RenderTestcmd.exe");
      }
    }

    if(!devLocation)
    {
      // look upwards on 32-bit to find the parent RenderTestcmd.
      wchar_t *slash = wcsrchr(RenderTestPath, L'\\');

      // remove the filename
      if(slash)
        *slash = 0;

      // remove the \\x86
      slash = wcsrchr(RenderTestPath, L'\\');

      if(slash)
        *slash = 0;

      // append path
      wcscat_s(RenderTestPath, L"\\RenderTestcmd.exe");
    }
#endif

    PROCESS_INFORMATION pi;
    STARTUPINFO si;
    SECURITY_ATTRIBUTES pSec;
    SECURITY_ATTRIBUTES tSec;

    RDCEraseEl(pi);
    RDCEraseEl(si);
    RDCEraseEl(pSec);
    RDCEraseEl(tSec);

    // hide the console window
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    pSec.nLength = sizeof(pSec);
    tSec.nLength = sizeof(tSec);

    // serialise to string with two chars per byte
    rdcstr optstr = opts.EncodeAsString();

    wchar_t *paramsAlloc = new wchar_t[2048];

    rdcstr debugLogfile = RDCGETLOGFILE();
    rdcwstr wdebugLogfile = StringFormat::UTF82Wide(debugLogfile);

    _snwprintf_s(
        paramsAlloc, 2047, 2047,
        L"\"%ls\" capaltbit --pid=%u --capfile=\"%ls\" --debuglog=\"%ls\" --capopts=\"%hs\"",
        RenderTestPath, pid, wcapturefile.c_str(), wdebugLogfile.c_str(), optstr.c_str());

    RDCDEBUG("params %ls", paramsAlloc);

    paramsAlloc[2047] = 0;

    wchar_t *commandLine = paramsAlloc;

    std::wstring cmdWithEnv;

    if(!env.empty())
    {
      cmdWithEnv = paramsAlloc;

      for(const EnvironmentModification &e : env)
      {
        rdcstr name = e.name.trimmed();
        rdcstr value = e.value;

        if(name == "")
          break;

        cmdWithEnv += L" +env-";
        switch(e.mod)
        {
          case EnvMod::Set: cmdWithEnv += L"replace"; break;
          case EnvMod::Append: cmdWithEnv += L"append"; break;
          case EnvMod::Prepend: cmdWithEnv += L"prepend"; break;
        }

        if(e.mod != EnvMod::Set)
        {
          switch(e.sep)
          {
            case EnvSep::Platform: cmdWithEnv += L"-platform"; break;
            case EnvSep::SemiColon: cmdWithEnv += L"-semicolon"; break;
            case EnvSep::Colon: cmdWithEnv += L"-colon"; break;
            case EnvSep::NoSep: break;
          }
        }

        cmdWithEnv += L" ";

        // escape the parameters
        for(size_t it = 0; it < name.size(); it++)
        {
          if(name[it] == '"')
          {
            name.insert(it, '\\');
            it++;
          }
        }

        for(size_t it = 0; it < value.size(); it++)
        {
          if(value[it] == '"')
          {
            value.insert(it, '\\');
            it++;
          }
        }

        if(name.back() == '\\')
          name += "\\";

        if(value.back() == '\\')
          value += "\\";

        cmdWithEnv += L"\"" + std::wstring(StringFormat::UTF82Wide(name).c_str()) + L"\" ";
        cmdWithEnv += L"\"" + std::wstring(StringFormat::UTF82Wide(value).c_str()) + L"\" ";
      }

      commandLine = (wchar_t *)cmdWithEnv.c_str();
    }

    BOOL retValue = CreateProcessW(NULL, commandLine, &pSec, &tSec, false,
                                   CREATE_NEW_CONSOLE | CREATE_SUSPENDED, NULL, NULL, &si, &pi);

    SAFE_DELETE_ARRAY(paramsAlloc);

    if(!retValue)
    {
      RDResult result;
#if RENDERTEST_OFFICIAL_BUILD
      SET_ERROR_RESULT(result, ResultCode::InternalError,
                       "Can't run 32-bit RenderTestcmd to capture 32-bit program.");
#else
      SET_ERROR_RESULT(
          result, ResultCode::InternalError,
          "Can't run 32-bit RenderTestcmd to capture 32-bit program."
          "If this is a locally built RenderTest you must build both 32-bit and 64-bit versions.");
#endif
      CloseHandle(hProcess);
      return {result, 0};
    }

    ResumeThread(pi.hThread);
    WaitForSingleObject(pi.hThread, INFINITE);
    CloseHandle(pi.hThread);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);

    if(waitForExit)
      WaitForSingleObject(hProcess, INFINITE);

    CloseHandle(hProcess);

    if(exitCode == 0)
    {
      RDResult result;
      SET_ERROR_RESULT(result, ResultCode::UnknownError,
                       "Encountered error while launching target 32-bit program.");
      return {result, 0};
    }

    if(exitCode < RENDERTEST_FirstTargetControlPort)
    {
      ResultCode code = (ResultCode)exitCode;

      RDResult result;
      SET_ERROR_RESULT(result, code, "32-bit RenderTestcmd returned '%s'", ToStr(code).c_str());
      return {code, 0};
    }

    return {ResultCode::Succeeded, (uint32_t)exitCode};
  }

  rdcwstr wRenderTestPath = StringFormat::UTF82Wide(StringFormat::Wide2UTF8(RenderTestPath));

  // prefer SetThreadContext-based injection when we can find a suspended thread
  // (e.g. the main thread of a process created with CREATE_SUSPENDED). This avoids
  // CreateRemoteThread which is detectable by anti-cheat systems.
  HANDLE hijackThread = FindSuspendedThread(pid);

  const char *rdoc_dll = STRINGIZE(RDOC_BASE_NAME);

  uintptr_t resultPtr = 0;
  if(hijackThread)
  {
    // phase A: load the DLL via the hijacked thread (full loader initialisation)
    resultPtr = InjectPayloadPhaseA(hProcess, hijackThread, wRenderTestPath);
  }
  else
  {
    // fallback for already-running processes: CreateRemoteThread based injection
    InjectDLLRemoteThread(hProcess, wRenderTestPath);
  }

  uintptr_t loc = FindRemoteDLL(pid, STRINGIZE(RDOC_BASE_NAME) ".dll");

  rdcpair<RDResult, uint32_t> result = {ResultCode::Succeeded, 0};

  if(loc == 0)
  {
    SET_ERROR_RESULT(
        result.first, ResultCode::InjectionFailed,
        "Failed to inject %s.dll into process. Check that the process did not crash or exit "
        "early in initialisation, e.g. if the working directory is incorrectly set.",
        rdoc_dll);
  }
  else
  {
    // safe to cast away the const as we know these functions don't modify the parameters

    if(hijackThread && resultPtr != 0)
    {
      // phase B/C: run the config calls via the hijacked thread, then let it
      // continue to the process entry point
      rdcarray<InjectConfigCall> configCalls;

      if(!capturefile.empty())
        AddConfigCall(configCalls, loc, "INTERNAL_SetCaptureFile", (void *)capturefile.c_str(),
                      capturefile.size() + 1);

      rdcstr debugLogfile = RDCGETLOGFILE();

      AddConfigCall(configCalls, loc, "INTERNAL_SetDebugLogFile", (void *)debugLogfile.c_str(),
                    debugLogfile.size() + 1);

      AddConfigCall(configCalls, loc, "INTERNAL_SetCaptureOptions", (CaptureOptions *)&opts,
                    sizeof(CaptureOptions));

      AddConfigCall(configCalls, loc, "INTERNAL_GetTargetControlIdent", &result.second,
                    sizeof(result.second));

      if(!env.empty())
      {
        for(const EnvironmentModification &e : env)
        {
          rdcstr name = e.name.trimmed();
          rdcstr value = e.value;
          EnvMod mod = e.mod;
          EnvSep sep = e.sep;

          if(name == "")
            break;

          AddConfigCall(configCalls, loc, "INTERNAL_EnvModName", (void *)name.c_str(),
                        name.size() + 1);
          AddConfigCall(configCalls, loc, "INTERNAL_EnvModValue", (void *)value.c_str(),
                        value.size() + 1);
          AddConfigCall(configCalls, loc, "INTERNAL_EnvSep", &sep, sizeof(sep));
          AddConfigCall(configCalls, loc, "INTERNAL_EnvMod", &mod, sizeof(mod));
        }

        // parameter is unused
        void *dummy = NULL;
        AddConfigCall(configCalls, loc, "INTERNAL_ApplyEnvMods", &dummy, sizeof(dummy));
      }

      InjectPayloadPhaseBC(hProcess, resultPtr, configCalls, &result.second, sizeof(result.second));
    }
    else if(hijackThread == NULL)
    {
      if(!capturefile.empty())
        InjectFunctionCallRemoteThread(hProcess, loc, "INTERNAL_SetCaptureFile",
                                       (void *)capturefile.c_str(), capturefile.size() + 1);

      rdcstr debugLogfile = RDCGETLOGFILE();

      InjectFunctionCallRemoteThread(hProcess, loc, "INTERNAL_SetDebugLogFile",
                                     (void *)debugLogfile.c_str(), debugLogfile.size() + 1);

      InjectFunctionCallRemoteThread(hProcess, loc, "INTERNAL_SetCaptureOptions",
                                     (CaptureOptions *)&opts, sizeof(CaptureOptions));

      InjectFunctionCallRemoteThread(hProcess, loc, "INTERNAL_GetTargetControlIdent",
                                     &result.second, sizeof(result.second));

      if(!env.empty())
      {
        for(const EnvironmentModification &e : env)
        {
          rdcstr name = e.name.trimmed();
          rdcstr value = e.value;
          EnvMod mod = e.mod;
          EnvSep sep = e.sep;

          if(name == "")
            break;

          InjectFunctionCallRemoteThread(hProcess, loc, "INTERNAL_EnvModName",
                                         (void *)name.c_str(), name.size() + 1);
          InjectFunctionCallRemoteThread(hProcess, loc, "INTERNAL_EnvModValue",
                                         (void *)value.c_str(), value.size() + 1);
          InjectFunctionCallRemoteThread(hProcess, loc, "INTERNAL_EnvSep", &sep, sizeof(sep));
          InjectFunctionCallRemoteThread(hProcess, loc, "INTERNAL_EnvMod", &mod, sizeof(mod));
        }

        // parameter is unused
        void *dummy = NULL;
        InjectFunctionCallRemoteThread(hProcess, loc, "INTERNAL_ApplyEnvMods", &dummy,
                                       sizeof(dummy));
      }
    }
  }

  if(hijackThread)
    CloseHandle(hijackThread);

  if(waitForExit)
    WaitForSingleObject(hProcess, INFINITE);

  CloseHandle(hProcess);

  return result;
}

uint32_t Process::LaunchProcess(const rdcstr &app, const rdcstr &workingDir, const rdcstr &cmdLine,
                                bool internal, ProcessResult *result)
{
  HANDLE hChildStdOutput_Rd = NULL, hChildStdError_Rd = NULL;

  rdcstr appPath = app;
  size_t len = appPath.length();
  rdcstr ext;
  if(len > 4)
    ext = strlower(appPath.substr(len - 4));
  if(ext != ".exe")
    appPath += ".exe";

  PROCESS_INFORMATION pi =
      RunProcess(appPath, workingDir, cmdLine, {}, internal, result ? &hChildStdOutput_Rd : NULL,
                 result ? &hChildStdError_Rd : NULL);

  if(pi.dwProcessId == 0)
  {
    if(!internal)
      RDCWARN("Couldn't launch process '%s'", appPath.c_str());

    if(hChildStdError_Rd != NULL)
      CloseHandle(hChildStdError_Rd);
    if(hChildStdOutput_Rd != NULL)
      CloseHandle(hChildStdOutput_Rd);

    return 0;
  }

  if(!internal)
    RDCLOG("Launched process '%s' with '%s'", appPath.c_str(), cmdLine.c_str());

  ResumeThread(pi.hThread);

  if(result)
  {
    result->strStdout = "";
    result->strStderror = "";

    char chBuf[4096];
    DWORD dwOutputRead, dwErrorRead;
    BOOL success = FALSE;
    rdcstr s;
    for(;;)
    {
      success = ReadFile(hChildStdOutput_Rd, chBuf, sizeof(chBuf), &dwOutputRead, NULL);
      s = rdcstr(chBuf, dwOutputRead);
      result->strStdout += s;

      if(!success && !dwOutputRead)
        break;
    }

    for(;;)
    {
      success = ReadFile(hChildStdError_Rd, chBuf, sizeof(chBuf), &dwErrorRead, NULL);
      s = rdcstr(chBuf, dwErrorRead);
      result->strStderror += s;

      if(!success && !dwErrorRead)
        break;
    }

    CloseHandle(hChildStdOutput_Rd);
    CloseHandle(hChildStdError_Rd);

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, (LPDWORD)&result->retCode);
  }

  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  return pi.dwProcessId;
}

uint32_t Process::LaunchScript(const rdcstr &script, const rdcstr &workingDir,
                               const rdcstr &argList, bool internal, ProcessResult *result)
{
  // Change parameters to invoke command interpreter
  rdcstr args = "/C " + script + " " + argList;

  return LaunchProcess("cmd.exe", workingDir, args, internal, result);
}

rdcpair<RDResult, uint32_t> Process::LaunchAndInjectIntoProcess(
    const rdcstr &app, const rdcstr &workingDir, const rdcstr &cmdLine,
    const rdcarray<EnvironmentModification> &env, const rdcstr &capturefile,
    const CaptureOptions &opts, bool waitForExit)
{
  void *func =
      GetProcAddress(GetModuleHandleA(STRINGIZE(RDOC_BASE_NAME) ".dll"), "INTERNAL_SetCaptureFile");

  if(func == NULL)
  {
    const char *rdoc_dll = STRINGIZE(RDOC_BASE_NAME);
    RDResult result;
    SET_ERROR_RESULT(result, ResultCode::InternalError,
                     "Can't find required export function in %s.dll - corrupted/missing file?",
                     rdoc_dll);
    return {result, 0};
  }

  if(get_basename(app) == "explorer.exe" || get_basename(app) == "dllhost.exe")
  {
    RDResult result;
    SET_ERROR_RESULT(
        result, ResultCode::InjectionFailed,
        "For safety reasons RenderTest does not support capturing executables with a "
        "reserved system filename such as '%s'. Please rename your executable to capture.",
        get_basename(app).c_str());
    return {result, 0};
  }

  PROCESS_INFORMATION pi = RunProcess(app, workingDir, cmdLine, env, false, NULL, NULL);

  if(pi.dwProcessId == 0)
  {
    RDResult result;
    SET_ERROR_RESULT(result, ResultCode::InjectionFailed, "Failed to launch process.");
    return {result, 0};
  }

  rdcpair<RDResult, uint32_t> ret = InjectIntoProcess(pi.dwProcessId, {}, capturefile, opts, false);

  CloseHandle(pi.hProcess);
  ResumeThread(pi.hThread);
  ResumeThread(pi.hThread);

  if(ret.second == 0 || ret.first != ResultCode::Succeeded)
  {
    CloseHandle(pi.hThread);
    return ret;
  }

  if(waitForExit)
    WaitForSingleObject(pi.hThread, INFINITE);

  CloseHandle(pi.hThread);

  return ret;
}

bool Process::CanGlobalHook()
{
  // all we need is admin rights and it's the caller's responsibility to ensure that.
  return true;
}

// to simplify the below code, rather than splitting by 32-bit/64-bit we split by native and Wow32.
// This means that for 32-bit code (whether it's on 32-bit OS or not) we just have native, and the
// Wow32 stuff is empty/unused. For 64-bit we use both. Thus the native registry key is always the
// same path regardless of the bitness we're running as and we don't have to move things around or
// have conditionals all over

struct GlobalHookData
{
  struct
  {
    HANDLE pipe = NULL;
    DWORD appinitEnabled = 0;
    rdcwstr appinitDLLs;
  } dataNative, dataWow32;

  int32_t finished = 0;
  Threading::ThreadHandle pipeThread = 0;
};

// utility function to close the registry keys, print an error, and quit
static RDResult HandleRegError(HKEY keyNative, HKEY keyWow32, LSTATUS ret, const char *msg)
{
  if(keyNative)
    RegCloseKey(keyNative);

  if(keyWow32)
    RegCloseKey(keyWow32);

  RDCLOG("Error with AppInit registry keys - %s (%d)", msg, ret);

  RETURN_ERROR_RESULT(ResultCode::InjectionFailed,
                      "Error updating registry to enable global hook.\n"
                      "Check that RenderTest is correctly running as administrator.");
}

#define REG_CHECK(msg)                                    \
  if(ret != ERROR_SUCCESS)                                \
  {                                                       \
    return HandleRegError(keyNative, keyWow32, ret, msg); \
  }

// function to backup the previous settings for AppInit, then enable it and write our own paths.
RDResult BackupAndChangeRegistry(GlobalHookData &hookdata, const rdcstr &shimpathWow32,
                                 const rdcstr &shimpathNative)
{
  HKEY keyNative = NULL;
  HKEY keyWow32 = NULL;

  // AppInit_DLLs requires short paths, but short paths can be disabled globally or on a per-volume
  // level. If short paths are disabled we'll get the long path back, we *always* expect the path to
  // get shorter because the shim filename is bigger than 8.3.

  DWORD nativeShortSize = GetShortPathNameW(StringFormat::UTF82Wide(shimpathNative).c_str(), NULL,
                                            (DWORD)shimpathNative.length());
  if(nativeShortSize == (DWORD)shimpathNative.length() + 1)
  {
    RETURN_ERROR_RESULT(
        ResultCode::FileIOFailed,
        "RenderTest is installed on a volume or system that has short paths disabled.\n"
        "For the global hook, short paths must be enabled where RenderTest is installed.");
  }

  if(!shimpathWow32.empty())
  {
    DWORD wow32ShortSize = GetShortPathNameW(StringFormat::UTF82Wide(shimpathWow32).c_str(), NULL,
                                             (DWORD)shimpathWow32.length());

    if(wow32ShortSize == (DWORD)shimpathWow32.length() + 1)
    {
      RETURN_ERROR_RESULT(
          ResultCode::FileIOFailed,
          "RenderTest is installed on a volume or system that has short paths disabled.\n"
          "For the global hook, short paths must be enabled where RenderTest is installed.");
    }
  }

  // open the native key
  LSTATUS ret = RegCreateKeyExA(HKEY_LOCAL_MACHINE,
                                "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Windows", 0, NULL,
                                0, KEY_READ | KEY_WRITE, NULL, &keyNative, NULL);

  REG_CHECK("Could not open AppInit key");

  // if we are doing Wow32, open that key as well
  if(!shimpathWow32.empty())
  {
    ret = RegCreateKeyExA(HKEY_LOCAL_MACHINE,
                          "SOFTWARE\\Wow6432Node\\Microsoft\\Windows NT\\CurrentVersion\\Windows",
                          0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &keyWow32, NULL);

    REG_CHECK("Could not open AppInit key");
  }

  const DWORD one = 1;

  // fetch the previous data for LoadAppInit_DLLs and AppInit_DLLs
  DWORD sz = 4;
  ret = RegGetValueA(keyNative, NULL, "LoadAppInit_DLLs", RRF_RT_REG_DWORD, NULL,
                     (void *)&hookdata.dataNative.appinitEnabled, &sz);
  REG_CHECK("Could not fetch LoadAppInit_DLLs");

  sz = 0;
  ret = RegGetValueW(keyNative, NULL, L"AppInit_DLLs", RRF_RT_ANY, NULL, NULL, &sz);
  if(ret == ERROR_MORE_DATA || ret == ERROR_SUCCESS)
  {
    hookdata.dataNative.appinitDLLs = rdcwstr(sz / sizeof(wchar_t));
    ret = RegGetValueW(keyNative, NULL, L"AppInit_DLLs", RRF_RT_ANY, NULL,
                       hookdata.dataNative.appinitDLLs.data(), &sz);
  }
  REG_CHECK("Could not fetch AppInit_DLLs");

  // set DWORD:1 for LoadAppInit_DLLs and convert our path to a short path then set it
  ret = RegSetValueExA(keyNative, "LoadAppInit_DLLs", 0, REG_DWORD, (const BYTE *)&one, sizeof(one));
  REG_CHECK("Could not set LoadAppInit_DLLs");

  rdcwstr shortpath(shimpathNative.size());
  GetShortPathNameW(StringFormat::UTF82Wide(shimpathNative).c_str(), shortpath.data(),
                    (DWORD)shortpath.length());

  ret = RegSetValueExW(keyNative, L"AppInit_DLLs", 0, REG_SZ, (const BYTE *)shortpath.data(),
                       DWORD(shortpath.length() * sizeof(wchar_t)));
  REG_CHECK("Could not set AppInit_DLLs");

  // if we're doing Wow32, repeat the process for those keys
  if(keyWow32)
  {
    sz = 4;
    ret = RegGetValueA(keyWow32, NULL, "LoadAppInit_DLLs", RRF_RT_REG_DWORD, NULL,
                       (void *)&hookdata.dataWow32.appinitEnabled, &sz);
    REG_CHECK("Could not fetch LoadAppInit_DLLs");

    sz = 0;
    ret = RegGetValueW(keyWow32, NULL, L"AppInit_DLLs", RRF_RT_ANY, NULL, NULL, &sz);
    if(ret == ERROR_MORE_DATA || ret == ERROR_SUCCESS)
    {
      hookdata.dataWow32.appinitDLLs = rdcwstr(sz / sizeof(wchar_t));
      ret = RegGetValueW(keyWow32, NULL, L"AppInit_DLLs", RRF_RT_ANY, NULL,
                         hookdata.dataWow32.appinitDLLs.data(), &sz);
    }
    REG_CHECK("Could not fetch AppInit_DLLs");

    ret = RegSetValueExA(keyWow32, "LoadAppInit_DLLs", 0, REG_DWORD, (const BYTE *)&one, sizeof(one));
    REG_CHECK("Could not set LoadAppInit_DLLs");

    shortpath = rdcwstr(shimpathWow32.size());
    GetShortPathNameW(StringFormat::UTF82Wide(shimpathWow32).c_str(), shortpath.data(),
                      (DWORD)shortpath.length());

    ret = RegSetValueExW(keyWow32, L"AppInit_DLLs", 0, REG_SZ, (const BYTE *)shortpath.data(),
                         DWORD(shortpath.length() * sizeof(wchar_t)));
    REG_CHECK("Could not set AppInit_DLLs");
  }

  std::wstring backup;

  // write a .reg file that contains the previous settings, so that if all else fails the user can
  // manually insert it back into the registry to restore everything.
  backup += L"Windows Registry Editor Version 5.00\n";
  backup += L"\n";
  backup += L"[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Windows]\n";
  backup += L"\"LoadAppInit_DLLs\"=dword:0000000";
  backup += (hookdata.dataNative.appinitEnabled ? L"1\n" : L"0\n");
  backup += L"\"AppInit_DLLs\"=\"";
  // we append with the C string so we don't add trailing NULLs into the text.
  backup += hookdata.dataNative.appinitDLLs.c_str();
  backup += L"\"\n";
  if(keyWow32)
  {
    backup += L"\n";
    backup +=
        L"[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Microsoft\\"
        L"Windows NT\\CurrentVersion\\Windows]\n";
    backup += L"\"LoadAppInit_DLLs\"=dword:0000000";
    backup += (hookdata.dataWow32.appinitEnabled ? L"1\n" : L"0\n");
    backup += L"\"AppInit_DLLs\"=\"";
    backup += hookdata.dataWow32.appinitDLLs.c_str();
    backup += L"\"\n";
  }

  if(keyNative)
    RegCloseKey(keyNative);

  if(keyWow32)
    RegCloseKey(keyWow32);

  keyNative = keyWow32 = NULL;

  // write it to disk but don't fail if we can't, just print it to the log and keep going.
  wchar_t reg_backup[MAX_PATH];
  GetTempPathW(MAX_PATH, reg_backup);
  wcscat_s(reg_backup, L"RENDERTEST_RestoreGlobalHook.reg");

  FILE *f = NULL;
  _wfopen_s(&f, reg_backup, L"w");
  if(f)
  {
    fputws(backup.c_str(), f);
    fclose(f);
  }
  else
  {
    RDCERR("Error opening registry backup file %ls", reg_backup);
    RDCERR("Backup registry data is:\n\n%ls\n\n", backup.c_str());
  }

  return RDResult();
}

// switch error-handling to print-and-continue, as we can't really do anything about it at this
// point and we want to continue restoring in case only one thing failed.
#undef REG_CHECK
#define REG_CHECK(msg)                                                      \
  if(ret != ERROR_SUCCESS)                                                  \
  {                                                                         \
    HandleRegError(keyNative, keyWow32, ret, "Could not open AppInit key"); \
  }

void RestoreRegistry(const GlobalHookData &hookdata)
{
  HKEY keyNative = NULL;
  HKEY keyWow32 = NULL;
  LSTATUS ret = RegCreateKeyExA(HKEY_LOCAL_MACHINE,
                                "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Windows", 0, NULL,
                                0, KEY_READ | KEY_WRITE, NULL, &keyNative, NULL);

  REG_CHECK("Could not open AppInit key");

#if ENABLED(RDOC_X64)
  ret = RegCreateKeyExA(HKEY_LOCAL_MACHINE,
                        "SOFTWARE\\Wow6432Node\\Microsoft\\Windows NT\\CurrentVersion\\Windows", 0,
                        NULL, 0, KEY_READ | KEY_WRITE, NULL, &keyWow32, NULL);

  REG_CHECK("Could not open AppInit key");
#endif

  // set the native values back to where they were
  ret = RegSetValueExA(keyNative, "LoadAppInit_DLLs", 0, REG_DWORD,
                       (const BYTE *)&hookdata.dataNative.appinitEnabled,
                       sizeof(hookdata.dataNative.appinitEnabled));
  REG_CHECK("Could not set LoadAppInit_DLLs");

  ret = RegSetValueExW(keyNative, L"AppInit_DLLs", 0, REG_SZ,
                       (const BYTE *)hookdata.dataNative.appinitDLLs.c_str(),
                       DWORD(hookdata.dataNative.appinitDLLs.length() * sizeof(wchar_t)));
  REG_CHECK("Could not set AppInit_DLLs");

  // if we opened it, restore the Wow32 values as well
  if(keyWow32)
  {
    ret = RegSetValueExA(keyWow32, "LoadAppInit_DLLs", 0, REG_DWORD,
                         (const BYTE *)&hookdata.dataWow32.appinitEnabled,
                         sizeof(hookdata.dataWow32.appinitEnabled));
    REG_CHECK("Could not set LoadAppInit_DLLs");

    ret = RegSetValueExW(keyWow32, L"AppInit_DLLs", 0, REG_SZ,
                         (const BYTE *)hookdata.dataWow32.appinitDLLs.c_str(),
                         DWORD(hookdata.dataWow32.appinitDLLs.length() * sizeof(wchar_t)));
    REG_CHECK("Could not set AppInit_DLLs");
  }
}

static GlobalHookData *globalHook = NULL;

// a thread we run in the background just to keep the pipes open and wait until we're ready to stop
// the global hook.
static void GlobalHookThread()
{
  Threading::SetCurrentThreadName("GlobalHookThread");

  // keep looping doing an atomic compare-exchange to check that finished is still 0
  while(Atomic::CmpExch32(&globalHook->finished, 0, 0) == 0)
  {
    // wake every quarter of a second to test again
    Threading::Sleep(250);
  }

  char exitData[32] = "exit";

  // write some data into the pipe and close it. The data is (currently) unimportant, just that it
  // causes the blocking read on the other end to succeed and close the program.
  DWORD dummy = 0;
  if(globalHook->dataNative.pipe)
  {
    WriteFile(globalHook->dataNative.pipe, exitData, (DWORD)sizeof(exitData), &dummy, NULL);
    CloseHandle(globalHook->dataNative.pipe);
  }

  if(globalHook->dataWow32.pipe)
  {
    WriteFile(globalHook->dataWow32.pipe, exitData, (DWORD)sizeof(exitData), &dummy, NULL);
    CloseHandle(globalHook->dataWow32.pipe);
  }
}

RDResult Process::StartGlobalHook(const rdcstr &pathmatch, const rdcstr &capturefile,
                                  const CaptureOptions &opts)
{
  if(pathmatch.empty())
  {
    RETURN_ERROR_RESULT(ResultCode::InvalidParameter,
                        "Invalid global hook parameter, empty path to match");
  }

  rdcstr RenderTestPath;
  FileIO::GetLibraryFilename(RenderTestPath);

  RenderTestPath = get_dirname(RenderTestPath);

  // the native RenderTestcmd.exe is always next to the dll. Wow32 will be somewhere else
  rdcstr cmdpathNative = RenderTestPath + "\\RenderTestcmd.exe";
  rdcstr cmdpathWow32;

  rdcstr shimpathNative = RenderTestPath;
  rdcstr shimpathWow32;

#if ENABLED(RDOC_X64)

  // native shim is just RenderTestshim64.dll
  shimpathNative = RenderTestPath + "\\RenderTestshim64.dll";

  // if it looks like we're in the development environment, look for the alternate bitness in the
  // corresponding folder
  int devLocation = RenderTestPath.find("\\x64\\Development");
  if(devLocation >= 0)
  {
    RenderTestPath.erase(devLocation, ~0U);

    shimpathWow32 = RenderTestPath + "\\Win32\\Development\\RenderTestshim32.dll";
    cmdpathWow32 = RenderTestPath + "\\Win32\\Development\\RenderTestcmd.exe";
  }
  else
  {
    devLocation = RenderTestPath.find("\\x64\\Release");

    if(devLocation >= 0)
    {
      RenderTestPath.erase(devLocation, ~0U);

      shimpathWow32 = RenderTestPath + "\\Win32\\Release\\RenderTestshim32.dll";
      cmdpathWow32 = RenderTestPath + "\\Win32\\Release\\RenderTestcmd.exe";
    }
  }

  // if we're not in the dev environment, assume it's under a x86\ subfolder
  if(devLocation < 0)
  {
    shimpathWow32 = RenderTestPath + "\\x86\\RenderTestshim32.dll";
    cmdpathWow32 = RenderTestPath + "\\x86\\RenderTestcmd.exe";
  }

#else

  // nothing fancy to do here for 32-bit, just point the shim next to our dll.
  shimpathNative = RenderTestPath + "\\RenderTestshim32.dll";

#endif

  GlobalHookData hookdata;

  // try to backup and change the registry settings to start loading our shim dlls. If that fails,
  // we bail out immediately
  RDResult regStatus = BackupAndChangeRegistry(hookdata, shimpathWow32, shimpathNative);
  if(regStatus != ResultCode::Succeeded)
    return regStatus;

  PROCESS_INFORMATION pi = {0};
  STARTUPINFO si = {0};
  SECURITY_ATTRIBUTES pSec = {0};
  SECURITY_ATTRIBUTES tSec = {0};
  pSec.nLength = sizeof(pSec);
  tSec.nLength = sizeof(tSec);

  si.cb = sizeof(si);

  // serialise to string with two chars per byte
  rdcstr optstr = opts.EncodeAsString();
  rdcstr debugLogfile = RDCGETLOGFILE();

  rdcstr params = StringFormat::Fmt(
      "\"%s\" globalhook --match \"%s\" --capfile \"%s\" --debuglog \"%s\" --capopts \"%s\"",
      cmdpathNative.c_str(), pathmatch.c_str(), capturefile.c_str(), debugLogfile.c_str(),
      optstr.c_str());

  rdcwstr paramsAlloc = StringFormat::UTF82Wide(params);

  // we'll be setting stdin
  si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;

  // hide the console window
  si.wShowWindow = SW_HIDE;

  // this is the end of the pipe that the child will inherit and use as stdin
  HANDLE childEnd = NULL;

  DWORD err;

  // create a pipe with the writing end for us, and the reading end as the child process's stdin
  {
    SECURITY_ATTRIBUTES pipeSec;
    pipeSec.nLength = sizeof(SECURITY_ATTRIBUTES);
    pipeSec.bInheritHandle = TRUE;
    pipeSec.lpSecurityDescriptor = NULL;

    BOOL res;
    res = CreatePipe(&childEnd, &hookdata.dataNative.pipe, &pipeSec, 0);

    if(!res)
    {
      err = GetLastError();
      RestoreRegistry(hookdata);
      RETURN_ERROR_RESULT(ResultCode::InternalError, "Could not create 32-bit stdin pipe (err %u)",
                          err);
    }

    // we don't want the child process to inherit our end
    res = SetHandleInformation(hookdata.dataNative.pipe, HANDLE_FLAG_INHERIT, 0);

    if(!res)
    {
      err = GetLastError();
      RestoreRegistry(hookdata);
      RETURN_ERROR_RESULT(ResultCode::InternalError,
                          "Could not make 32-bit stdin pipe inheritable (err %u)", err);
    }

    si.hStdInput = childEnd;
  }

  // launch the process
  BOOL retValue = CreateProcessW(NULL, &paramsAlloc[0], &pSec, &tSec, true, CREATE_NEW_CONSOLE,
                                 NULL, NULL, &si, &pi);

  err = GetLastError();

  // we don't need this end anymore, the child has it
  CloseHandle(childEnd);

  if(retValue == FALSE)
  {
    CloseHandle(hookdata.dataNative.pipe);
    RestoreRegistry(hookdata);
    RETURN_ERROR_RESULT(ResultCode::InternalError, "Can't launch RenderTestcmd from '%s' (err %u)",
                        cmdpathNative.c_str(), err);
  }

  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  RDCEraseEl(pi);

// repeat the process for the Wow32 RenderTestcmd
#if ENABLED(RDOC_X64)
  params = StringFormat::Fmt(
      "\"%s\" globalhook --match \"%s\" --capfile \"%s\" --debuglog \"%s\" --capopts \"%s\"",
      cmdpathWow32.c_str(), pathmatch.c_str(), capturefile.c_str(), debugLogfile.c_str(),
      optstr.c_str());

  paramsAlloc = StringFormat::UTF82Wide(params);

  {
    SECURITY_ATTRIBUTES pipeSec;
    pipeSec.nLength = sizeof(SECURITY_ATTRIBUTES);
    pipeSec.bInheritHandle = TRUE;
    pipeSec.lpSecurityDescriptor = NULL;

    BOOL res;
    res = CreatePipe(&childEnd, &hookdata.dataWow32.pipe, &pipeSec, 0);

    if(!res)
    {
      err = GetLastError();
      RestoreRegistry(hookdata);
      RETURN_ERROR_RESULT(ResultCode::InternalError, "Could not create 64-bit stdin pipe (err %u)",
                          err);
    }

    res = SetHandleInformation(hookdata.dataWow32.pipe, HANDLE_FLAG_INHERIT, 0);

    if(!res)
    {
      err = GetLastError();
      RestoreRegistry(hookdata);
      RETURN_ERROR_RESULT(ResultCode::InternalError,
                          "Could not make 64-bit stdin pipe inheritable (err %u)", err);
    }

    si.hStdInput = childEnd;
  }

  retValue = CreateProcessW(NULL, &paramsAlloc[0], &pSec, &tSec, true, CREATE_NEW_CONSOLE, NULL,
                            NULL, &si, &pi);

  err = GetLastError();

  // we don't need this end anymore
  CloseHandle(childEnd);

  if(retValue == FALSE)
  {
    CloseHandle(hookdata.dataNative.pipe);
    CloseHandle(hookdata.dataWow32.pipe);
    RestoreRegistry(hookdata);
    RETURN_ERROR_RESULT(ResultCode::InternalError, "Can't launch RenderTestcmd from '%s' (err %u)",
                        cmdpathWow32.c_str(), err);
  }

  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
#endif

  // set static global pointer with our data, and launch the thread
  globalHook = new GlobalHookData;
  *globalHook = hookdata;

  globalHook->pipeThread = Threading::CreateThread(&GlobalHookThread);

  return RDResult();
}

bool Process::IsGlobalHookActive()
{
  return globalHook != NULL;
}
void Process::StopGlobalHook()
{
  if(!globalHook)
    return;

  // set the finished flag and join to the thread so it closes the pipes (and so the child
  // processes)
  Atomic::Inc32(&globalHook->finished);

  Threading::JoinThread(globalHook->pipeThread);
  Threading::CloseThread(globalHook->pipeThread);

  // restore the registry settings from before we started
  RestoreRegistry(*globalHook);

  delete globalHook;
  globalHook = NULL;
}

bool Process::IsModuleLoaded(const rdcstr &module)
{
  return GetModuleHandleA(module.c_str()) != NULL;
}

void *Process::LoadModule(const rdcstr &module)
{
  HMODULE mod = GetModuleHandleA(module.c_str());
  if(mod != NULL)
    return mod;

  return LoadLibraryA(module.c_str());
}

void *Process::GetFunctionAddress(void *module, const rdcstr &function)
{
  if(module == NULL)
    return NULL;

  return (void *)GetProcAddress((HMODULE)module, function.c_str());
}

uint32_t Process::GetCurrentPID()
{
  return (uint32_t)GetCurrentProcessId();
}

void Process::Shutdown()
{
  // nothing to do
}
