// Log, settings file, vtable hook (the Windows-specific part)
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <signal.h>
#include "core.h"
#include "game.h"
#include "session.h"
#include "online.h"
static char g_dir[MAX_PATH];
int g_instance = 1;
static FILE* g_log;
static char g_cfg[4096];
uint64_t now_ms(void) { return GetTickCount64(); }
static CRITICAL_SECTION g_logcs; static int g_logcs_ok;
static uint64_t g_t0;
static void log_write(const char* line, int forward) {
    if (!g_logcs_ok) return;
    char stamp[240]; uint64_t t = GetTickCount64() - g_t0;
    snprintf(stamp, sizeof stamp, "%02u:%02u.%u %s", (unsigned)(t / 60000), (unsigned)(t / 1000 % 60), (unsigned)(t / 100 % 10), line);
    EnterCriticalSection(&g_logcs);
    if (g_log) { fputs(line, g_log); fputc('\n', g_log); fflush(g_log); }
    LeaveCriticalSection(&g_logcs);
    log_ring_push(stamp);                 // outside the lock: keep the lock order
    if (forward) net_forward_log(line);
}
void logf_(const char* fmt, ...) {
    char line[1024]; va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
    log_write(line, 1);
}
void logf_raw(const char* line) { log_write(line, 0); }
void logf_remote(const char* who, const char* text) { char l[300]; snprintf(l, sizeof l, "[%s] %s", who, text); log_write(l, 0); }
const char* mod_dir(void) { return g_dir; }
void cfg_load(void) {
    char path[MAX_PATH + 32]; snprintf(path, sizeof path, "%sbbr2net.ini", g_dir);
    FILE* f = fopen(path, "rb");
    // bbr2net.ini is for development only (shadow car test, dev=1 shortcuts): not created when missing, defaults apply
    g_cfg[0] = '\n'; size_t n = 0;
    if (f) { n = fread(g_cfg + 1, 1, sizeof g_cfg - 3, f); fclose(f); }
    g_cfg[n + 1] = '\n'; g_cfg[n + 2] = 0;
}
static const char* cfg_find(const char* key) {
    size_t kl = strlen(key);
    for (const char* p = g_cfg; (p = strchr(p, '\n')) != 0; ) {
        p++;
        while (*p == ' ' || *p == '\t') p++;
        if (!strncmp(p, key, kl)) { const char* q = p + kl; while (*q == ' ' || *q == '\t') q++; if (*q == '=') return q + 1; }
    }
    return 0;
}
double cfg_num(const char* key, double def) { const char* v = cfg_find(key); return v ? strtod(v, 0) : def; }
void cfg_vec3(const char* key, float* out, float dx, float dy, float dz) {
    out[0] = dx; out[1] = dy; out[2] = dz;
    const char* v = cfg_find(key); if (!v) return;
    char* e;
    for (int i = 0; i < 3; i++) { out[i] = (float)strtod(v, &e); if (e == v) break; v = e; while (*v == ',' || *v == ' ') v++; }
}
int cfg_str(const char* key, char* out, int cap) {
    out[0] = 0; const char* v = cfg_find(key); if (!v) return 0;
    while (*v == ' ' || *v == '\t') v++;
    int n = 0; while (*v && *v != '\r' && *v != '\n' && *v != '#' && n < cap - 1) out[n++] = *v++;
    while (n && (out[n-1] == ' ' || out[n-1] == '\t')) n--;
    out[n] = 0; return n > 0;
}
static void modoff(void* a, char* out, int cap) {
    HMODULE m = 0; char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &m) && m) {
        GetModuleFileNameA(m, name, MAX_PATH); char* b = strrchr(name, '\\');
        snprintf(out, cap, "%s+0x%llx", b ? b + 1 : name, (unsigned long long)((uint8_t*)a - (uint8_t*)m));
    } else snprintf(out, cap, "%p", a);
}
static LONG CALLBACK veh_handler(EXCEPTION_POINTERS* e) {
    static volatile LONG count;
    DWORD c = e->ExceptionRecord->ExceptionCode;
    if (c != EXCEPTION_ACCESS_VIOLATION && c != EXCEPTION_ILLEGAL_INSTRUCTION && c != EXCEPTION_INT_DIVIDE_BY_ZERO
        && c != EXCEPTION_STACK_OVERFLOW && c != EXCEPTION_PRIV_INSTRUCTION && c != 0xC0000409) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&count) > 6) return EXCEPTION_CONTINUE_SEARCH;
    char where[300], st[900]; int k = 0; modoff(e->ExceptionRecord->ExceptionAddress, where, sizeof where);
    ULONG_PTR* sp = (ULONG_PTR*)e->ContextRecord->Rsp; st[0] = 0;
    for (int i = 0, n = 0; i < 512 && n < 10 && k < 800; i++) {   // in-module addresses on the stack (rough backtrace)
        ULONG_PTR v; if (IsBadReadPtr(sp + i, 8)) break; v = sp[i];
        HMODULE m; if (v > 0x10000 && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)v, &m)) {
            char t[200]; modoff((void*)v, t, sizeof t); k += snprintf(st + k, sizeof st - k, " %s", t); n++; }
    }
    LOG("!!! EXCEPTION %08lx address %s access %p thread %lu", (unsigned long)c, where,
        (void*)(e->ExceptionRecord->NumberParameters > 1 ? e->ExceptionRecord->ExceptionInformation[1] : 0), (unsigned long)GetCurrentThreadId());
    LOG("    stack:%s", st);
    return EXCEPTION_CONTINUE_SEARCH;
}
static void on_abort(int sig) { LOG("!!! abort() called (signal %d)", sig); }
void* vt_hook(void** vt, int slot, void* fn) {
    DWORD old; void* orig = vt[slot];
    if (!VirtualProtect(&vt[slot], 8, PAGE_READWRITE, &old)) return 0;
    vt[slot] = fn;
    VirtualProtect(&vt[slot], 8, old, &old);
    return orig;
}
void core_init(void) {
    uint8_t* base = (uint8_t*)GetModuleHandleA(0);
    if (!base) return;
    GetModuleFileNameA(0, g_dir, MAX_PATH);
    char* s = strrchr(g_dir, '\\'); if (!s) return;
    if (_stricmp(s + 1, "Game_x64.exe")) return;            // another exe (crash reporter etc.): do not touch
    if (pe_timestamp(base) != BUILD_TS) {                    // another build (e.g. GOG): only leave a note
        char exe[MAX_PATH]; strcpy(exe, g_dir); strcpy(s + 1, "bbr2mod\\"); CreateDirectoryA(g_dir, 0);
        char path[MAX_PATH + 32]; snprintf(path, sizeof path, "%snetlog.txt", g_dir);
        FILE* f = fopen(path, "w");
        if (f) { WIN32_FILE_ATTRIBUTE_DATA fa; unsigned long long sz = 0;
            if (GetFileAttributesExA(exe, GetFileExInfoStandard, &fa)) sz = ((unsigned long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
            fprintf(f, "%s\nUNSUPPORTED GAME VERSION: the mod is disabled.\nexe=%s\ntimestamp=0x%08x (expected 0x%08x), size=%llu bytes\n"
                       "This mod only supports one build of the game. Please report these lines to the mod author.\n", BBR2NET_VERSION, exe, pe_timestamp(base), BUILD_TS, sz); fclose(f); }
        return;
    }
    strcpy(s + 1, "bbr2mod\\");
    CreateDirectoryA(g_dir, 0);
    // if a second copy runs from the same folder it uses its own log file
    static HANDLE inst; char path[MAX_PATH + 32];
    for (g_instance = 1; g_instance <= 4; g_instance++) {
        char mx[64]; snprintf(mx, sizeof mx, "Local\\bbr2net_instance_%d", g_instance);
        inst = CreateMutexA(0, TRUE, mx);
        if (inst && GetLastError() != ERROR_ALREADY_EXISTS) break;
        if (inst) CloseHandle(inst);
    }
    if (g_instance == 1) snprintf(path, sizeof path, "%snetlog.txt", g_dir);
    else snprintf(path, sizeof path, "%snetlog%d.txt", g_dir, g_instance);
    { char prev[MAX_PATH + 48]; snprintf(prev, sizeof prev, "%.*s_prev.txt", (int)strlen(path) - 4, path); MoveFileExA(path, prev, MOVEFILE_REPLACE_EXISTING); }
    g_log = fopen(path, "w");
    // SDL hint: read gamepads while the window is in the background too (for two copies on one PC)
    SetEnvironmentVariableA("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");
    InitializeCriticalSection(&g_logcs); g_logcs_ok = 1; g_t0 = GetTickCount64();
    LOG("%s (copy %d)", BBR2NET_VERSION, g_instance);
    AddVectoredExceptionHandler(1, veh_handler); signal(SIGABRT, on_abort);
    LOG("exe timestamp 0x%08x, base %p", pe_timestamp(base), base);
    cfg_load();
    race_install(base);
    online_install(base);
    ui_install(base);
    net_start();
}
