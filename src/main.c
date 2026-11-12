#include <windows.h>
#include "core.h"
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); proxy_init(); core_init(); }
    return TRUE;
}
