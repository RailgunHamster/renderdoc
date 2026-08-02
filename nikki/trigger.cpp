// Remote capture trigger: calls RENDERTEST_GetAPI + TriggerCapture in the game process.
// Usage: trigger.exe <pid>
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstring>

static uintptr_t FindModuleBase(DWORD pid, const char *name)
{
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
  if(snap == INVALID_HANDLE_VALUE)
    return 0;
  MODULEENTRY32 me;
  me.dwSize = sizeof(me);
  uintptr_t ret = 0;
  if(Module32First(snap, &me))
  {
    do
    {
      if(_stricmp(me.szModule, name) == 0)
      {
        ret = (uintptr_t)me.modBaseAddr;
        break;
      }
    } while(Module32Next(snap, &me));
  }
  CloseHandle(snap);
  return ret;
}

int main(int argc, char **argv)
{
  if(argc < 2)
  {
    printf("usage: trigger.exe <pid>\n");
    return 1;
  }
  DWORD pid = (DWORD)atoi(argv[1]);

  uintptr_t modBase = FindModuleBase(pid, "rendertest.dll");
  if(!modBase)
  {
    printf("rendertest.dll not found in process %u (module enum may be blocked)\n", pid);
    return 1;
  }
  printf("rendertest.dll base = %p\n", (void *)modBase);

  // find RENDERTEST_GetAPI in our own copy of the dll
  HMODULE local = GetModuleHandleA("rendertest.dll");
  if(!local)
  {
    printf("rendertest.dll not loaded locally, loading...\n");
    local = LoadLibraryA("D:\\git\\rendertst-nikki\\nikki\\rendertest.dll");
  }
  uintptr_t getApiLocal = (uintptr_t)GetProcAddress(local, "RENDERTEST_GetAPI");
  if(!getApiLocal)
  {
    printf("could not resolve RENDERTEST_GetAPI\n");
    return 1;
  }
  uintptr_t getApiRemote = modBase + (getApiLocal - (uintptr_t)local);

  printf("RENDERTEST_GetAPI remote = %p\n", (void *)getApiRemote);

  // stub:
  //   sub rsp, 0x28
  //   xor ecx, ecx                 ; eRENDERDOC_API_Version_1_6_0 = 0
  //   mov rax, imm64(getApiRemote)
  //   call rax                     ; rax = api table
  //   test rax, rax
  //   je done
  //   call qword ptr [rax+0x78]    ; TriggerCapture()
  // done:
  //   add rsp, 0x28
  //   ret
  unsigned char stub[64] = {0};
  int o = 0;
  stub[o++] = 0x48; stub[o++] = 0x83; stub[o++] = 0xEC; stub[o++] = 0x28;
  stub[o++] = 0x31; stub[o++] = 0xC9;
  stub[o++] = 0x48; stub[o++] = 0xB8;
  memcpy(stub + o, &getApiRemote, 8); o += 8;
  stub[o++] = 0xFF; stub[o++] = 0xD0;
  stub[o++] = 0x48; stub[o++] = 0x85; stub[o++] = 0xC0;
  stub[o++] = 0x74; stub[o++] = 0x08;
  stub[o++] = 0xFF; stub[o++] = 0x50; stub[o++] = 0x78;
  stub[o++] = 0x48; stub[o++] = 0x83; stub[o++] = 0xC4; stub[o++] = 0x28;
  stub[o++] = 0xC3;

  HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                    PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
                                FALSE, pid);
  if(!hProcess)
  {
    printf("OpenProcess failed: %u\n", GetLastError());
    return 1;
  }

  void *remote = VirtualAllocEx(hProcess, NULL, sizeof(stub), MEM_COMMIT, PAGE_EXECUTE_READWRITE);
  if(!remote)
  {
    printf("VirtualAllocEx failed: %u\n", GetLastError());
    CloseHandle(hProcess);
    return 1;
  }
  WriteProcessMemory(hProcess, remote, stub, sizeof(stub), NULL);

  HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)remote, NULL, 0,
                                      NULL);
  if(!hThread)
  {
    printf("CreateRemoteThread failed: %u\n", GetLastError());
    CloseHandle(hProcess);
    return 1;
  }
  printf("trigger thread created, waiting...\n");
  WaitForSingleObject(hThread, 10000);
  CloseHandle(hThread);
  printf("done\n");
  return 0;
}
