// version.dll proxy: forwards the exports to the real version.dll in the system folder.
#include <windows.h>
#include "core.h"
#define FUNCS X(GetFileVersionInfoA) X(GetFileVersionInfoByHandle) X(GetFileVersionInfoExA) X(GetFileVersionInfoExW) \
    X(GetFileVersionInfoSizeA) X(GetFileVersionInfoSizeExA) X(GetFileVersionInfoSizeExW) X(GetFileVersionInfoSizeW) \
    X(GetFileVersionInfoW) X(VerFindFileA) X(VerFindFileW) X(VerInstallFileA) X(VerInstallFileW) X(VerLanguageNameA) \
    X(VerLanguageNameW) X(VerQueryValueA) X(VerQueryValueW)
#define X(n) void* p_##n;
FUNCS
#undef X
#define X(n) __attribute__((naked)) void px_##n(void) { __asm__("jmp *p_" #n "(%rip)"); }
FUNCS
#undef X
void proxy_init(void) {
    char path[MAX_PATH + 16];
    UINT n = GetSystemDirectoryA(path, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    strcpy(path + n, "\\version.dll");
    HMODULE h = LoadLibraryA(path);
    if (!h) return;
#define X(n) p_##n = (void*)GetProcAddress(h, #n);
    FUNCS
#undef X
}
