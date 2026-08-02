/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2015-2026 Baldur Karlsson
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

#pragma once

// Simple x64 inline hooking helpers. These patch the entry of a function with
// "mov rax, imm64; jmp rax" and build a trampoline containing the original bytes
// so the real function can still be called. This is a fallback for processes that
// bypass GetProcAddress/IAT hooking (e.g. bound delay-load imports).

#if defined(_M_X64)

#include <windows.h>
#include <stdint.h>

// Heartbeat debug helper: overwrites a file with the current tick + tag.
// Used to localise where a process hangs (e.g. wrapped device usage in the
// game) - the last written line shows the most recent activity point.
inline void Heartbeat(const char *tag)
{
  FILE *f = NULL;
  fopen_s(&f, "D:\\git\\renderdoc-nikki\\nikki\\heartbeat.txt", "w");
  if(f)
  {
    fprintf(f, "t=%llu pid=%d %s\n", (unsigned long long)GetTickCount64(),
            (int)GetCurrentProcessId(), tag);
    fclose(f);
  }
}

// Returns a trampoline for 'func' (or NULL), after patching 'func' to jump to 'hookFunc'.
inline void *InstallInlineHook(void *func, void *hookFunc)
{
  if(func == NULL || hookFunc == NULL)
    return NULL;

  const size_t patchLen = 12;  // mov rax, imm64 (10) + jmp rax (2)

  // read the original protection
  DWORD oldProtect = 0;
  if(!VirtualProtect(func, patchLen, PAGE_EXECUTE_READWRITE, &oldProtect))
    return NULL;

  // build the trampoline: original bytes + mov rax, imm64(func+patchLen); jmp rax
  // Allocate at a FIXED low address rather than letting VirtualAlloc pick: the
  // default placement can land inside the game's main thread stack region (e.g.
  // 0x90c2xxxx/0x915dxxxx/0x90C10000), so when the game later calls through the
  // trampoline the stack pushes hit unmapped pages. Sweep the low 2GB at 1MB
  // steps (game exe sits at 0x140000000, system DLLs at 0x7FFx..., thread
  // stacks typically 0x9xxxxxxx). If every candidate is reserved by the game's
  // memory pools, fall back to MEM_TOP_DOWN so the trampoline lands in high
  // address space - far away from the thread stacks.
  void *trampoline = NULL;
  for(uintptr_t addr = 0x10000000; addr < 0x7F000000ULL && trampoline == NULL; addr += 0x100000)
  {
    trampoline = VirtualAlloc((LPVOID)addr, patchLen + 14, MEM_COMMIT | MEM_RESERVE,
                              PAGE_EXECUTE_READWRITE);
  }
  if(trampoline == NULL)
    trampoline = VirtualAlloc(NULL, patchLen + 14, MEM_COMMIT | MEM_RESERVE | MEM_TOP_DOWN,
                              PAGE_EXECUTE_READWRITE);

  if(trampoline)
  {
    uint8_t *t = (uint8_t *)trampoline;
    memcpy(t, func, patchLen);
    t[patchLen] = 0x48;  // mov rax, imm64
    t[patchLen + 1] = 0xB8;
    uintptr_t rest = (uintptr_t)func + patchLen;
    memcpy(t + patchLen + 2, &rest, 8);
    t[patchLen + 10] = 0xFF;  // jmp rax
    t[patchLen + 11] = 0xE0;
    FlushInstructionCache(GetCurrentProcess(), trampoline, patchLen + 14);
  }

  // patch the original: mov rax, imm64(hookFunc); jmp rax
  uint8_t *f = (uint8_t *)func;
  f[0] = 0x48;
  f[1] = 0xB8;
  uintptr_t h = (uintptr_t)hookFunc;
  memcpy(f + 2, &h, 8);
  f[10] = 0xFF;
  f[11] = 0xE0;
  FlushInstructionCache(GetCurrentProcess(), func, patchLen);

  VirtualProtect(func, patchLen, oldProtect, &oldProtect);

  return trampoline;
}

// save the original entry bytes of 'func' (for restore-call-repatch usage)
inline bool SaveInlineHookBytes(void *func, uint8_t *out12)
{
  if(func == NULL || out12 == NULL)
    return false;
  memcpy(out12, func, 12);
  return true;
}

// restore original bytes (undo the inline patch)
inline void RestoreInlineHookBytes(void *func, const uint8_t *orig12)
{
  if(func == NULL || orig12 == NULL)
    return;
  DWORD oldProtect = 0;
  if(VirtualProtect(func, 12, PAGE_EXECUTE_READWRITE, &oldProtect))
  {
    memcpy(func, orig12, 12);
    FlushInstructionCache(GetCurrentProcess(), func, 12);
    VirtualProtect(func, 12, oldProtect, &oldProtect);
  }
}

// (re)apply the inline patch: mov rax, imm64(hookFunc); jmp rax
inline void PatchInlineHookBytes(void *func, void *hookFunc)
{
  if(func == NULL || hookFunc == NULL)
    return;
  DWORD oldProtect = 0;
  if(VirtualProtect(func, 12, PAGE_EXECUTE_READWRITE, &oldProtect))
  {
    uint8_t *f = (uint8_t *)func;
    f[0] = 0x48;
    f[1] = 0xB8;
    uintptr_t h = (uintptr_t)hookFunc;
    memcpy(f + 2, &h, 8);
    f[10] = 0xFF;
    f[11] = 0xE0;
    FlushInstructionCache(GetCurrentProcess(), func, 12);
    VirtualProtect(func, 12, oldProtect, &oldProtect);
  }
}

#else

inline void *InstallInlineHook(void *func, void *hookFunc)
{
  return NULL;
}

#endif
