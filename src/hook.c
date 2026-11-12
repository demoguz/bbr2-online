// Inline hook: replaces the first 'len' bytes of the target (must be position-independent instructions) with an absolute jump.
#include <windows.h>
#include "core.h"
void* inline_hook(uint8_t* target, int len, const uint8_t* expect, void* detour) {
    if (len < 14 || memcmp(target, expect, len)) return 0;
    uint8_t* tr = (uint8_t*)VirtualAlloc(0, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tr) return 0;
    memcpy(tr, target, len);
    tr[len] = 0xFF; tr[len + 1] = 0x25; memset(tr + len + 2, 0, 4); uint64_t back = (uint64_t)(uintptr_t)(target + len); memcpy(tr + len + 6, &back, 8);
    DWORD old;
    if (!VirtualProtect(target, len, PAGE_EXECUTE_READWRITE, &old)) return 0;
    uint8_t patch[32]; patch[0] = 0xFF; patch[1] = 0x25; memset(patch + 2, 0, 4); uint64_t to = (uint64_t)(uintptr_t)detour; memcpy(patch + 6, &to, 8);
    for (int i = 14; i < len; i++) patch[i] = 0x90;
    memcpy(target, patch, len);
    VirtualProtect(target, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, len);
    return tr;
}
